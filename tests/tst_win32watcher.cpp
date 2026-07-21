#include <QtTest>

#ifdef Q_OS_WIN

#include "core/TaskQueue.h"
#include "watcher/Win32Watcher.h"

using core::IndexTask;
using core::TaskQueue;
using watcher::Win32Watcher;

class Win32WatcherTest : public QObject {
    Q_OBJECT

    static void writeFile(const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
        file.write(content);
    }

    /// Drains the queue into a flat list of paths, ignoring task kind — the
    /// tests below only care that *some* task named the path they touched.
    static QStringList drainPaths(TaskQueue& queue) {
        QStringList paths;
        while (queue.size() > 0) {
            if (const auto task = queue.pop())
                paths.append(task->path);
        }
        return paths;
    }

private slots:
    void changeAtRootIsDetected() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        TaskQueue queue;
        Win32Watcher watcher(&queue);
        watcher.start();
        watcher.watchTree(dir.path());
        // Arming ReadDirectoryChangesW happens on the watcher thread, woken
        // asynchronously by watchTree(); without this, the write below can
        // race ahead of the OS actually watching the directory yet.
        QTest::qWait(200);

        writeFile(dir.filePath("fresh.txt"), "hello watcher");

        QTRY_VERIFY_WITH_TIMEOUT(queue.size() > 0, 5000);
        const QStringList paths = drainPaths(queue);
        QVERIFY(paths.contains(dir.filePath("fresh.txt")));
    }

    void nestedSubdirectoryChangeIsDetectedWithoutRewatching() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir("nested"));

        TaskQueue queue;
        Win32Watcher watcher(&queue);
        watcher.start();
        // A single watchTree() on the root must cover the pre-existing
        // "nested" subdirectory too (bWatchSubtree=TRUE) — no per-directory
        // watch call is needed, unlike the portable QFileSystemWatcher backend.
        watcher.watchTree(dir.path());
        QTest::qWait(200); // let the watcher thread arm the watch first

        writeFile(dir.filePath("nested/inner.txt"), "nested change");

        QTRY_VERIFY_WITH_TIMEOUT(queue.size() > 0, 5000);
        const QStringList paths = drainPaths(queue);
        QVERIFY(paths.contains(dir.filePath("nested/inner.txt")));
    }

    void removalEnqueuesCleanupTasks() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath("gone.txt");
        writeFile(filePath, "ephemeral");

        TaskQueue queue;
        Win32Watcher watcher(&queue);
        watcher.start();
        watcher.watchTree(dir.path());
        QTest::qWait(200); // let the watcher thread arm the watch first

        QVERIFY(QFile::remove(filePath));

        QTRY_VERIFY_WITH_TIMEOUT(queue.size() > 0, 5000);
        const QStringList paths = drainPaths(queue);
        QVERIFY(paths.contains(filePath));
    }

    void unwatchTreeStopsFurtherEvents() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        TaskQueue queue;
        Win32Watcher watcher(&queue);
        watcher.start();
        watcher.watchTree(dir.path());
        QTest::qWait(200);

        watcher.unwatchTree(dir.path());
        QTest::qWait(200); // let the removal actually process

        writeFile(dir.filePath("later.txt"), "should not be observed");
        QTest::qWait(300);
        QCOMPARE(queue.size(), 0);
    }
};

QTEST_GUILESS_MAIN(Win32WatcherTest)

#else // !Q_OS_WIN

// Win32Watcher only exists on Windows; nothing to test elsewhere.
class Win32WatcherTest : public QObject {
    Q_OBJECT
};

QTEST_GUILESS_MAIN(Win32WatcherTest)

#endif

#include "tst_win32watcher.moc"
