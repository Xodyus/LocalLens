#include <QtTest>

#include "core/IndexerWorker.h"
#include "core/TaskQueue.h"
#include "database/IndexStore.h"

using core::IndexTask;
using core::IndexerWorker;
using core::TaskQueue;

class IndexerWorkerTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    void writeFile(const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
        file.write(content);
    }

private slots:
    void indexesQueuedFileAndStopsCleanly() {
        const QString dbPath = m_dir.filePath("worker1.db");
        const QString docPath = m_dir.filePath("doc.txt");
        writeFile(docPath, "sphinx of black quartz judge my vow");

        TaskQueue queue;
        IndexerWorker worker(dbPath, &queue, "worker-conn-1");
        QSignalSpy indexChanged(&worker, &IndexerWorker::indexChanged);
        worker.start();

        queue.push({IndexTask::Kind::AddOrUpdate, docPath});
        QVERIFY(indexChanged.wait(5000));

        queue.stop();
        QVERIFY(worker.wait(5000));

        db::IndexStore reader(dbPath, "reader-conn-1");
        QVERIFY2(reader.open(), qPrintable(reader.lastError()));
        QCOMPARE(reader.documentCount(), 1);
        QCOMPARE(reader.search({"sphinx"}).size(), size_t(1));
    }

    void rescanIndexesSupportedFilesRecursively() {
        const QString dbPath = m_dir.filePath("worker2.db");
        const QString root = m_dir.filePath("tree");
        QVERIFY(QDir().mkpath(root + "/sub"));
        writeFile(root + "/a.md", "alpha content here");
        writeFile(root + "/sub/b.md", "beta content here");
        writeFile(root + "/skip.bin", "binary payload");

        TaskQueue queue;
        IndexerWorker worker(dbPath, &queue, "worker-conn-2");
        worker.start();
        queue.push({IndexTask::Kind::Rescan, root});

        db::IndexStore reader(dbPath, "reader-conn-2");
        QVERIFY2(reader.open(), qPrintable(reader.lastError()));
        QTRY_COMPARE_WITH_TIMEOUT(reader.documentCount(), qint64(2), 5000);
        QCOMPARE(reader.search({"alpha"}).size(), size_t(1));

        queue.stop();
        QVERIFY(worker.wait(5000));
    }

    void removeDirTaskDropsDocuments() {
        const QString dbPath = m_dir.filePath("worker3.db");
        const QString root = m_dir.filePath("gone");
        QVERIFY(QDir().mkpath(root));
        writeFile(root + "/c.txt", "ephemeral gamma text");

        TaskQueue queue;
        IndexerWorker worker(dbPath, &queue, "worker-conn-3");
        worker.start();
        queue.push({IndexTask::Kind::Rescan, root});

        db::IndexStore reader(dbPath, "reader-conn-3");
        QVERIFY2(reader.open(), qPrintable(reader.lastError()));
        QTRY_COMPARE_WITH_TIMEOUT(reader.documentCount(), qint64(1), 5000);

        queue.push({IndexTask::Kind::RemoveDir, root});
        QTRY_COMPARE_WITH_TIMEOUT(reader.documentCount(), qint64(0), 5000);

        queue.stop();
        QVERIFY(worker.wait(5000));
    }
};

QTEST_GUILESS_MAIN(IndexerWorkerTest)
#include "tst_indexerworker.moc"
