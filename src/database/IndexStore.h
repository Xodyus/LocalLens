#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <vector>

namespace db {

struct SearchHit {
    qint64 docId = 0;
    QString path;
    double score = 0.0;
    int matchedTerms = 0;
};

/// SQLite-backed inverted index.
///
/// Schema: documents (catalog) / terms (vocabulary) / postings (term_id ->
/// doc_id with term frequency). Search scores hits with BM25.
///
/// Paths: stored document paths use '/' separators on every platform (what
/// QDir/QDirIterator produce); removeDocumentsUnder() normalizes native '\'
/// input, but writers should store '/'-separated paths.
///
/// Threading: a QSqlDatabase connection is only valid on the thread that
/// created it, so construct one IndexStore per thread, each with a unique
/// connection name, all pointing at the same database file. WAL mode lets
/// the search thread read while the indexer thread writes.
class IndexStore {
public:
    IndexStore(QString databasePath, QString connectionName);
    ~IndexStore();

    IndexStore(const IndexStore&) = delete;
    IndexStore& operator=(const IndexStore&) = delete;

    /// Opens the connection and creates the schema. Call from the owning thread.
    bool open();
    QString lastError() const { return m_lastError; }

    // ---- Writer API (indexer thread) ----

    /// Inserts or replaces a document and its postings in one transaction.
    bool upsertDocument(const QString& path, qint64 mtimeMs, qint64 sizeBytes,
                        const QHash<QString, int>& termFrequencies);
    bool removeDocument(const QString& path);
    /// Removes every indexed document whose path starts with `dirPath`
    /// (either separator style accepted).
    bool removeDocumentsUnder(const QString& dirPath);
    /// True if the stored mtime/size differ from the given ones (or the
    /// document is unknown) — used to skip unchanged files cheaply.
    bool needsReindex(const QString& path, qint64 mtimeMs, qint64 sizeBytes) const;

    // ---- Reader API (search thread) ----

    /// BM25-ranked search over already-tokenized query terms.
    std::vector<SearchHit> search(const QStringList& terms, int limit = 50) const;
    /// Terms starting with `prefix`, most widespread first (for autocomplete).
    QStringList suggestTerms(const QString& prefix, int limit = 8) const;
    /// Indexed paths whose parent directory is exactly `dirPath` (no nested
    /// subdirectories) — lets a caller diff against what's really on disk to
    /// catch deletions a non-recursive directory-changed notification can't
    /// name individually.
    QStringList documentPathsDirectlyUnder(const QString& dirPath) const;

    // ---- Metrics ----

    qint64 documentCount() const;
    qint64 termCount() const;
    qint64 databaseSizeBytes() const;

private:
    QSqlDatabase connection() const;
    bool execSchema();
    qint64 termId(const QString& term, bool createIfMissing);
    /// Drops vocabulary rows that no longer appear in any posting.
    bool pruneOrphanTerms();

    QString m_databasePath;
    QString m_connectionName;
    mutable QString m_lastError;

    // BM25 parameters (standard defaults).
    static constexpr double kBm25K1 = 1.2;
    static constexpr double kBm25B = 0.75;
};

} // namespace db
