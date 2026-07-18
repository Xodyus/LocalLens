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
};

QTEST_GUILESS_MAIN(FilesystemWatcherTest)
#include "tst_filesystemwatcher.moc"
