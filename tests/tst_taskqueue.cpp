#include <QtTest>

#include "core/TaskQueue.h"

#include <thread>

using core::IndexTask;
using core::TaskQueue;

class TaskQueueTest : public QObject {
    Q_OBJECT

    static IndexTask addTask(const QString& path) {
        return {IndexTask::Kind::AddOrUpdate, path};
    }

private slots:
    void popReturnsTasksInFifoOrder() {
        TaskQueue queue;
        queue.push(addTask("/a.txt"));
        queue.push(addTask("/b.txt"));
        QCOMPARE(queue.size(), 2);

        QCOMPARE(queue.pop()->path, QString("/a.txt"));
        QCOMPARE(queue.pop()->path, QString("/b.txt"));
        QCOMPARE(queue.size(), 0);
    }

    void identicalPendingTasksCoalesce() {
        TaskQueue queue;
        queue.push(addTask("/same.txt"));
        queue.push(addTask("/same.txt"));
        QCOMPARE(queue.size(), 1);

        // Same path but a different kind is different work — keep it.
        queue.push({IndexTask::Kind::Remove, "/same.txt"});
        QCOMPARE(queue.size(), 2);
    }

    void popBlocksUntilAProducerPushes() {
        TaskQueue queue;
        std::thread producer([&queue] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            queue.push(addTask("/late.txt"));
        });

        const auto task = queue.pop(); // must block until the push above
        producer.join();
        QVERIFY(task.has_value());
        QCOMPARE(task->path, QString("/late.txt"));
    }

    void stopWakesBlockedConsumers() {
        TaskQueue queue;
        std::optional<IndexTask> received = addTask("/sentinel");
        std::thread consumer([&] { received = queue.pop(); });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        queue.stop();
        consumer.join();
        QVERIFY(!received.has_value());
    }

    void stopDiscardsPendingWorkAndRejectsNew() {
        TaskQueue queue;
        queue.push(addTask("/pending.txt"));
        queue.stop();

        QVERIFY(queue.isStopped());
        QCOMPARE(queue.size(), 0);
        QVERIFY(!queue.pop().has_value());

        queue.push(addTask("/ignored.txt"));
        QCOMPARE(queue.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TaskQueueTest)
#include "tst_taskqueue.moc"
