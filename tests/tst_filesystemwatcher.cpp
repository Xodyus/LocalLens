#include <QtTest>

#include "core/TaskQueue.h"
#include "watcher/FilesystemWatcher.h"

using core::IndexTask;
using core::TaskQueue;
using watcher::FilesystemWatcher;

class FilesystemWatcherTest : public QObject {
    Q_OBJECT

private slots:
    void directoryChangeEnqueuesRescan() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        TaskQueue queue;
        FilesystemWatcher fsw(&queue);
        fsw.watchTree(dir.path());

        // Creating a file counts as a change to the watched directory.
        QFile file(dir.filePath("fresh.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello watcher");
        file.close();

        QTRY_VERIFY_WITH_TIMEOUT(queue.size() > 0, 5000);
        const auto task = queue.pop();
        QVERIFY(task.has_value());
        QVERIFY(task->kind == IndexTask::Kind::Rescan);
        QCOMPARE(task->path, dir.path());
    }

    void newSubdirectoriesGetWatchedToo() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        TaskQueue queue;
        FilesystemWatcher fsw(&queue);
        fsw.watchTree(dir.path());

        QVERIFY(QDir(dir.path()).mkdir("nested"));
        QTRY_VERIFY_WITH_TIMEOUT(queue.size() > 0, 5000);
        while (queue.size() > 0)
            queue.pop(); // drain the events from creating the directory

        // A change inside the new subdirectory must now be seen as well.
        QFile file(dir.filePath("nested/inner.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("nested change");
        file.close();

        QTRY_VERIFY_WITH_TIMEOUT(queue.size() > 0, 5000);
        const auto task = queue.pop();
        QVERIFY(task.has_value());
        QVERIFY(task->kind == IndexTask::Kind::Rescan);
        QCOMPARE(task->path, dir.filePath("nested"));
    }

    void directoryChangeAlsoEnqueuesReconcileDir() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        TaskQueue queue;
        FilesystemWatcher fsw(&queue);
        fsw.watchTree(dir.path());

        QFile file(dir.filePath("fresh.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello watcher");
        file.close();

        QTRY_VERIFY_WITH_TIMEOUT(queue.size() >= 2, 5000);
        bool sawRescan = false;
        bool sawReconcile = false;
        while (queue.size() > 0) {
            const auto task = queue.pop();
            sawRescan |= task->kind == IndexTask::Kind::Rescan;
            sawReconcile |= task->kind == IndexTask::Kind::ReconcileDir;
        }
        QVERIFY(sawRescan);
        QVERIFY(sawReconcile);
    }

    void unwatchTreeStopsFurtherEvents() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        TaskQueue queue;
        FilesystemWatcher fsw(&queue);
        fsw.watchTree(dir.path());
        fsw.unwatchTree(dir.path());

        QFile file(dir.filePath("later.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("should not be observed");
        file.close();

        // Give a (now-removed) watch a chance to misfire before asserting it didn't.
        QTest::qWait(300);
        QCOMPARE(queue.size(), 0);
    }
};

QTEST_GUILESS_MAIN(FilesystemWatcherTest)
#include "tst_filesystemwatcher.moc"
