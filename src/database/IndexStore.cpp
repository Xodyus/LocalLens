#include "database/IndexStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace db {
namespace {

/// RAII transaction guard: rolls back unless commit() succeeds.
class Transaction {
public:
    explicit Transaction(QSqlDatabase& database) : m_db(database) {
        m_active = m_db.transaction();
    }
    ~Transaction() {
        if (m_active)
            m_db.rollback();
    }
    bool begun() const { return m_active; }
    bool commit() {
        if (m_active && m_db.commit()) {
            m_active = false;
            return true;
        }
        return false;
    }

private:
    QSqlDatabase& m_db;
    bool m_active = false;
};

QString escapeLikePrefix(QString prefix) {
    prefix.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
    return prefix + '%';
}

} // namespace

IndexStore::IndexStore(QString databasePath, QString connectionName)
    : m_databasePath(std::move(databasePath)), m_connectionName(std::move(connectionName)) {}

IndexStore::~IndexStore() {
    {
        QSqlDatabase database = QSqlDatabase::database(m_connectionName, /*open=*/false);
        if (database.isValid())
            database.close();
    }
    // The QSqlDatabase handle above must be destroyed before the connection
    // is removed, hence the scope.
    QSqlDatabase::removeDatabase(m_connectionName);
}

QSqlDatabase IndexStore::connection() const {
    return QSqlDatabase::database(m_connectionName);
}

bool IndexStore::open() {
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    database.setDatabaseName(m_databasePath);
    if (!database.open()) {
        m_lastError = database.lastError().text();
        return false;
    }
    return execSchema();
}

bool IndexStore::execSchema() {
    QSqlDatabase database = connection();
    const QStringList statements = {
        // WAL allows the search connection to read while the indexer writes.
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=NORMAL",
        "PRAGMA foreign_keys=ON",
        "CREATE TABLE IF NOT EXISTS documents ("
        "  id INTEGER PRIMARY KEY,"
        "  path TEXT NOT NULL UNIQUE,"
        "  mtime INTEGER NOT NULL,"
        "  size INTEGER NOT NULL,"
        "  token_count INTEGER NOT NULL,"
        "  indexed_at INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS terms ("
        "  id INTEGER PRIMARY KEY,"
        "  term TEXT NOT NULL UNIQUE)",
        "CREATE TABLE IF NOT EXISTS postings ("
        "  term_id INTEGER NOT NULL REFERENCES terms(id) ON DELETE CASCADE,"
        "  doc_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
        "  frequency INTEGER NOT NULL,"
        "  PRIMARY KEY (term_id, doc_id)) WITHOUT ROWID",
        "CREATE INDEX IF NOT EXISTS idx_postings_doc ON postings(doc_id)",
    };
    for (const QString& sql : statements) {
        QSqlQuery query(database);
        if (!query.exec(sql)) {
            m_lastError = query.lastError().text();
            return false;
        }
    }
    return true;
}

qint64 IndexStore::termId(const QString& term, bool createIfMissing) {
    QSqlDatabase database = connection();
    QSqlQuery select(database);
    select.prepare("SELECT id FROM terms WHERE term = ?");
    select.addBindValue(term);
    if (select.exec() && select.next()) {
        const qint64 id = select.value(0).toLongLong();
        select.finish(); // release the statement so the transaction can commit
        return id;
    }
    select.finish();

    if (!createIfMissing)
        return -1;

    QSqlQuery insert(database);
    insert.prepare("INSERT INTO terms (term) VALUES (?)");
    insert.addBindValue(term);
    if (!insert.exec()) {
        m_lastError = insert.lastError().text();
        return -1;
    }
    return insert.lastInsertId().toLongLong();
}

bool IndexStore::upsertDocument(const QString& path, qint64 mtimeMs, qint64 sizeBytes,
                                const QHash<QString, int>& termFrequencies) {
    QSqlDatabase database = connection();
    Transaction tx(database);
    if (!tx.begun()) {
        m_lastError = database.lastError().text();
        return false;
    }

    int tokenCount = 0;
    for (const int frequency : termFrequencies)
        tokenCount += frequency;

    QSqlQuery upsert(database);
    upsert.prepare(
        "INSERT INTO documents (path, mtime, size, token_count, indexed_at) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "  mtime = excluded.mtime, size = excluded.size,"
        "  token_count = excluded.token_count, indexed_at = excluded.indexed_at "
        "RETURNING id");
    upsert.addBindValue(path);
    upsert.addBindValue(mtimeMs);
    upsert.addBindValue(sizeBytes);
    upsert.addBindValue(tokenCount);
    upsert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!upsert.exec() || !upsert.next()) {
        m_lastError = upsert.lastError().text();
        return false;
    }
    const qint64 docId = upsert.value(0).toLongLong();
    // SQLite refuses to COMMIT while any statement is still "in progress";
    // finish() resets the RETURNING statement now that its row is consumed.
    upsert.finish();

    // Remember which terms this document used before re-indexing, so we can
    // tell afterward which of them lost their last posting (see below).
    std::vector<qint64> oldTermIds;
    {
        QSqlQuery oldTerms(database);
        oldTerms.prepare("SELECT term_id FROM postings WHERE doc_id = ?");
        oldTerms.addBindValue(docId);
        if (oldTerms.exec()) {
            while (oldTerms.next())
                oldTermIds.push_back(oldTerms.value(0).toLongLong());
        }
    }

    // Re-indexing replaces the document's postings wholesale.
    QSqlQuery clear(database);
    clear.prepare("DELETE FROM postings WHERE doc_id = ?");
    clear.addBindValue(docId);
    if (!clear.exec()) {
        m_lastError = clear.lastError().text();
        return false;
    }

    // Note: bindValue(index, ...) rather than addBindValue — the latter keeps
    // appending positions when a prepared query is re-executed in a loop.
    QSqlQuery insertPosting(database);
    insertPosting.prepare("INSERT INTO postings (term_id, doc_id, frequency) VALUES (?, ?, ?)");
    for (auto it = termFrequencies.cbegin(); it != termFrequencies.cend(); ++it) {
        const qint64 id = termId(it.key(), /*createIfMissing=*/true);
        if (id < 0)
            return false;
        insertPosting.bindValue(0, id);
        insertPosting.bindValue(1, docId);
        insertPosting.bindValue(2, it.value());
        if (!insertPosting.exec()) {
            m_lastError = insertPosting.lastError().text();
            return false;
        }
    }

    // A term this document dropped (edited out, or the doc changed entirely)
    // may now have zero postings anywhere. Checked per-term rather than the
    // full-table sweep pruneOrphanTerms() does, since this runs on every
    // re-index — bounded by this document's own old vocabulary, not the
    // whole database.
    if (!oldTermIds.empty()) {
        QSqlQuery stillUsed(database);
        stillUsed.prepare("SELECT 1 FROM postings WHERE term_id = ? LIMIT 1");
        QSqlQuery deleteTerm(database);
        deleteTerm.prepare("DELETE FROM terms WHERE id = ?");
        for (const qint64 oldId : oldTermIds) {
            stillUsed.bindValue(0, oldId);
            if (!stillUsed.exec()) {
                m_lastError = stillUsed.lastError().text();
                return false;
            }
            const bool orphaned = !stillUsed.next();
            stillUsed.finish();
            if (orphaned) {
                deleteTerm.bindValue(0, oldId);
                if (!deleteTerm.exec()) {
                    m_lastError = deleteTerm.lastError().text();
                    return false;
                }
            }
        }
    }

    if (!tx.commit()) {
        m_lastError = database.lastError().text();
        return false;
    }
    return true;
}

bool IndexStore::removeDocument(const QString& path) {
    QSqlDatabase database = connection();
    Transaction tx(database);
    if (!tx.begun()) {
        m_lastError = database.lastError().text();
        return false;
    }
    QSqlQuery query(database);
    query.prepare("DELETE FROM documents WHERE path = ?");
    query.addBindValue(path);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (!pruneOrphanTerms())
        return false;
    if (!tx.commit()) {
        m_lastError = database.lastError().text();
        return false;
    }
    return true;
}

bool IndexStore::removeDocumentsUnder(const QString& dirPath) {
    // Stored paths always use '/' separators; accept native '\' input too.
    QString prefix = QDir::fromNativeSeparators(dirPath);
    if (!prefix.endsWith('/'))
        prefix += '/';

    QSqlDatabase database = connection();
    Transaction tx(database);
    if (!tx.begun()) {
        m_lastError = database.lastError().text();
        return false;
    }
    QSqlQuery query(database);
    query.prepare("DELETE FROM documents WHERE path LIKE ? ESCAPE '\\'");
    query.addBindValue(escapeLikePrefix(prefix));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (!pruneOrphanTerms())
        return false;
    if (!tx.commit()) {
        m_lastError = database.lastError().text();
        return false;
    }
    return true;
}

bool IndexStore::pruneOrphanTerms() {
    // Deleting documents cascades postings away but leaves vocabulary rows
    // behind; without this, termCount() grows forever.
    QSqlQuery query(connection());
    if (!query.exec("DELETE FROM terms WHERE NOT EXISTS ("
                    "SELECT 1 FROM postings WHERE postings.term_id = terms.id)")) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool IndexStore::needsReindex(const QString& path, qint64 mtimeMs, qint64 sizeBytes) const {
    QSqlQuery query(connection());
    query.prepare("SELECT mtime, size FROM documents WHERE path = ?");
    query.addBindValue(path);
    if (!query.exec() || !query.next())
        return true; // unknown document
    return query.value(0).toLongLong() != mtimeMs || query.value(1).toLongLong() != sizeBytes;
}

std::vector<SearchHit> IndexStore::search(const QStringList& terms, int limit) const {
    std::vector<SearchHit> hits;
    if (terms.isEmpty())
        return hits;

    QSqlDatabase database = connection();

    qint64 totalDocs = 0;
    double avgTokenCount = 0.0;
    {
        QSqlQuery stats(database);
        if (stats.exec("SELECT COUNT(*), COALESCE(AVG(token_count), 0) FROM documents") &&
            stats.next()) {
            totalDocs = stats.value(0).toLongLong();
            avgTokenCount = stats.value(1).toDouble();
        }
    }
    if (totalDocs == 0)
        return hits;
    if (avgTokenCount <= 0.0)
        avgTokenCount = 1.0;

    struct Accumulator {
        double score = 0.0;
        int matchedTerms = 0;
    };
    QHash<qint64, Accumulator> scores;

    // A term repeated in the query must score (and count) only once.
    QStringList uniqueTerms = terms;
    uniqueTerms.removeDuplicates();

    // Resolve each term to its id and document frequency once, then walk its
    // postings — idf is a per-term constant, so computing df per result row
    // (as a correlated subquery would) is pure waste.
    QSqlQuery termQuery(database);
    termQuery.prepare(
        "SELECT t.id, COUNT(p.doc_id) FROM terms t "
        "JOIN postings p ON p.term_id = t.id "
        "WHERE t.term = ? GROUP BY t.id");

    QSqlQuery postings(database);
    postings.prepare(
        "SELECT p.doc_id, p.frequency, d.token_count "
        "FROM postings p "
        "JOIN documents d ON d.id = p.doc_id "
        "WHERE p.term_id = ?");

    for (const QString& term : uniqueTerms) {
        termQuery.bindValue(0, term);
        if (!termQuery.exec()) {
            m_lastError = termQuery.lastError().text();
            return hits;
        }
        if (!termQuery.next())
            continue; // term not in the vocabulary
        const qint64 id = termQuery.value(0).toLongLong();
        const double df = termQuery.value(1).toDouble();

        // BM25 idf: rewards terms that appear in few documents.
        const double idf = std::log(1.0 + (totalDocs - df + 0.5) / (df + 0.5));

        postings.bindValue(0, id);
        if (!postings.exec()) {
            m_lastError = postings.lastError().text();
            return hits;
        }
        while (postings.next()) {
            const qint64 docId = postings.value(0).toLongLong();
            const double tf = postings.value(1).toDouble();
            const double docLen = std::max(1.0, postings.value(2).toDouble());

            // BM25 tf saturation with document-length normalization.
            const double tfNorm = (tf * (kBm25K1 + 1.0)) /
                                  (tf + kBm25K1 * (1.0 - kBm25B + kBm25B * docLen / avgTokenCount));

            Accumulator& acc = scores[docId];
            acc.score += idf * tfNorm;
            acc.matchedTerms += 1;
        }
    }

    hits.reserve(scores.size());
    for (auto it = scores.cbegin(); it != scores.cend(); ++it)
        hits.push_back({it.key(), QString(), it.value().score, it.value().matchedTerms});

    // Documents matching more query terms first, then by score.
    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.matchedTerms != b.matchedTerms)
            return a.matchedTerms > b.matchedTerms;
        return a.score > b.score;
    });
    if (static_cast<int>(hits.size()) > limit)
        hits.resize(static_cast<size_t>(limit));

    QSqlQuery pathQuery(database);
    pathQuery.prepare("SELECT path FROM documents WHERE id = ?");
    for (SearchHit& hit : hits) {
        pathQuery.bindValue(0, hit.docId);
        if (pathQuery.exec() && pathQuery.next())
            hit.path = pathQuery.value(0).toString();
    }
    return hits;
}

QStringList IndexStore::suggestTerms(const QString& prefix, int limit) const {
    QStringList suggestions;
    if (prefix.isEmpty())
        return suggestions;

    QSqlQuery query(connection());
    query.prepare(
        "SELECT t.term FROM terms t "
        "JOIN postings p ON p.term_id = t.id "
        "WHERE t.term LIKE ? ESCAPE '\\' "
        "GROUP BY t.id ORDER BY COUNT(p.doc_id) DESC, t.term ASC LIMIT ?");
    query.addBindValue(escapeLikePrefix(prefix.toLower()));
    query.addBindValue(limit);
    if (query.exec()) {
        while (query.next())
            suggestions.append(query.value(0).toString());
    }
    return suggestions;
}

QStringList IndexStore::documentPathsDirectlyUnder(const QString& dirPath) const {
    QString prefix = QDir::fromNativeSeparators(dirPath);
    if (!prefix.endsWith('/'))
        prefix += '/';
    QString escaped = prefix;
    escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");

    QStringList paths;
    QSqlQuery query(connection());
    // Excluding a second '/' after the prefix keeps this to direct children —
    // exactly what one non-recursive directory-changed notification covers.
    query.prepare("SELECT path FROM documents WHERE path LIKE ? ESCAPE '\\' "
                 "AND path NOT LIKE ? ESCAPE '\\'");
    query.addBindValue(escaped + '%');
    query.addBindValue(escaped + "%/%");
    if (query.exec()) {
        while (query.next())
            paths.append(query.value(0).toString());
    }
    return paths;
}

qint64 IndexStore::documentCount() const {
    QSqlQuery query(connection());
    if (query.exec("SELECT COUNT(*) FROM documents") && query.next())
        return query.value(0).toLongLong();
    return 0;
}

qint64 IndexStore::termCount() const {
    QSqlQuery query(connection());
    if (query.exec("SELECT COUNT(*) FROM terms") && query.next())
        return query.value(0).toLongLong();
    return 0;
}

qint64 IndexStore::databaseSizeBytes() const {
    qint64 total = QFileInfo(m_databasePath).size();
    total += QFileInfo(m_databasePath + "-wal").size(); // 0 if absent
    return total;
}

} // namespace db
