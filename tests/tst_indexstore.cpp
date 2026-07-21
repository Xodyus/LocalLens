#include <QtTest>

#include "core/Tokenizer.h"
#include "database/IndexStore.h"

using core::Tokenizer;
using db::IndexStore;

class IndexStoreTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<IndexStore> m_store;
    int m_docCounter = 0;

    void indexText(const QString& path, const QString& text) {
        ++m_docCounter;
        QVERIFY2(m_store->upsertDocument(path, m_docCounter, text.size(),
                                         Tokenizer::termFrequencies(text)),
                 qPrintable(m_store->lastError()));
    }

private slots:
    void init() {
        m_store = std::make_unique<IndexStore>(
            m_dir.filePath(QStringLiteral("test_%1.db").arg(++m_docCounter)),
            QStringLiteral("test_conn_%1").arg(m_docCounter));
        QVERIFY2(m_store->open(), qPrintable(m_store->lastError()));
    }

    void cleanup() { m_store.reset(); }

    void indexAndFindDocument() {
        indexText("/docs/apples.md", "apple orchard harvest apple cider");
        indexText("/docs/space.md", "rocket launch orbital telemetry");

        const auto hits = m_store->search({"apple"});
        QCOMPARE(hits.size(), size_t(1));
        QCOMPARE(hits[0].path, QString("/docs/apples.md"));
        QCOMPARE(m_store->documentCount(), 2);
    }

    void ranksHigherFrequencyFirst() {
        indexText("/a.txt", "kernel kernel kernel kernel scheduling");
        indexText("/b.txt", "kernel notes about other things entirely today");

        const auto hits = m_store->search({"kernel"});
        QCOMPARE(hits.size(), size_t(2));
        QCOMPARE(hits[0].path, QString("/a.txt"));
        QVERIFY(hits[0].score > hits[1].score);
    }

    void multiTermMatchesRankAboveSingle() {
        indexText("/both.txt", "database index performance tuning");
        indexText("/one.txt", "database migrations checklist procedure");

        const auto hits = m_store->search({"database", "index"});
        QCOMPARE(hits.size(), size_t(2));
        QCOMPARE(hits[0].path, QString("/both.txt"));
        QCOMPARE(hits[0].matchedTerms, 2);
    }

    void reindexReplacesOldPostings() {
        indexText("/note.txt", "alpha beta gamma");
        indexText("/note.txt", "delta epsilon zeta");

        QVERIFY(m_store->search({"alpha"}).empty());
        QCOMPARE(m_store->search({"delta"}).size(), size_t(1));
        QCOMPARE(m_store->documentCount(), 1);
    }

    void removeDocumentDropsItFromSearch() {
        indexText("/gone.txt", "ephemeral content vanishes");
        QVERIFY(m_store->removeDocument("/gone.txt"));
        QVERIFY(m_store->search({"ephemeral"}).empty());
        QCOMPARE(m_store->documentCount(), 0);
    }

    void removeDocumentsUnderPrefix() {
        indexText("/watched/a.txt", "sunflower field");
        indexText("/watched/sub/b.txt", "sunflower seeds");
        indexText("/other/c.txt", "sunflower oil");

        QVERIFY(m_store->removeDocumentsUnder("/watched"));
        const auto hits = m_store->search({"sunflower"});
        QCOMPARE(hits.size(), size_t(1));
        QCOMPARE(hits[0].path, QString("/other/c.txt"));
    }

    void needsReindexTracksMtimeAndSize() {
        indexText("/tracked.txt", "stable content");
        const qint64 mtime = m_docCounter; // indexText used the counter as mtime
        const qint64 size = QString("stable content").size();

        QVERIFY(!m_store->needsReindex("/tracked.txt", mtime, size));
        QVERIFY(m_store->needsReindex("/tracked.txt", mtime + 1, size));
        QVERIFY(m_store->needsReindex("/tracked.txt", mtime, size + 1));
        QVERIFY(m_store->needsReindex("/never-seen.txt", 0, 0));
    }

    void suggestsTermsByPrefixAndPopularity() {
        indexText("/1.txt", "compiler compile");
        indexText("/2.txt", "compiler linker");
        indexText("/3.txt", "compiler flags");

        const auto suggestions = m_store->suggestTerms("comp");
        QVERIFY(suggestions.size() >= 2);
        QCOMPARE(suggestions.first(), QString("compiler")); // appears in 3 docs
        QVERIFY(suggestions.contains("compile"));
    }

    void likeWildcardsInPrefixAreEscaped() {
        indexText("/1.txt", "percent20encoding notes");
        QVERIFY(m_store->suggestTerms("%").isEmpty());
        QVERIFY(m_store->suggestTerms("_").isEmpty());
    }

    void duplicateQueryTermsScoreOnce() {
        indexText("/a.txt", "apple pie recipe collection");
        indexText("/b.txt", "banana bread baking basics");

        const auto once = m_store->search({"apple"});
        const auto twice = m_store->search({"apple", "apple"});
        QCOMPARE(twice.size(), once.size());
        QCOMPARE(twice[0].matchedTerms, 1);
        QCOMPARE(twice[0].score, once[0].score);
    }

    void removalsPruneOrphanTerms() {
        indexText("/a.txt", "unique zebra words");
        indexText("/b.txt", "shared zebra content");
        QCOMPARE(m_store->termCount(), 5);

        QVERIFY(m_store->removeDocument("/a.txt"));
        // "unique" and "words" appeared only in a.txt; the vocabulary keeps
        // just "shared", "zebra", "content".
        QCOMPARE(m_store->termCount(), 3);
    }

    void reindexingPrunesOrphanedTerms() {
        indexText("/note.txt", "alpha beta gamma");
        QCOMPARE(m_store->termCount(), 3);

        indexText("/note.txt", "beta gamma delta"); // "alpha" now used nowhere
        QCOMPARE(m_store->termCount(), 3); // beta, gamma, delta (alpha pruned)
        QVERIFY(m_store->search({"alpha"}).empty());
    }

    void documentPathsDirectlyUnderReturnsOnlyDirectChildren() {
        indexText("/watched/a.txt", "one");
        indexText("/watched/b.txt", "two");
        indexText("/watched/sub/c.txt", "three");
        indexText("/other/d.txt", "four");

        const QStringList direct = m_store->documentPathsDirectlyUnder("/watched");
        QCOMPARE(direct.size(), 2);
        QVERIFY(direct.contains("/watched/a.txt"));
        QVERIFY(direct.contains("/watched/b.txt"));
        QVERIFY(!direct.contains("/watched/sub/c.txt"));
        QVERIFY(!direct.contains("/other/d.txt"));
    }

    void removeDocumentsUnderAcceptsNativeSeparators() {
        indexText("C:/docs/a.txt", "sunflower field");
        indexText("C:/keep/b.txt", "sunflower oil");

        QVERIFY(m_store->removeDocumentsUnder("C:\\docs"));
        const auto hits = m_store->search({"sunflower"});
        QCOMPARE(hits.size(), size_t(1));
        QCOMPARE(hits[0].path, QString("C:/keep/b.txt"));
    }

    void emptySearchReturnsNothing() {
        indexText("/1.txt", "something here");
        QVERIFY(m_store->search({}).empty());
        QVERIFY(m_store->search({"missingterm"}).empty());
    }
};

QTEST_GUILESS_MAIN(IndexStoreTest)
#include "tst_indexstore.moc"
