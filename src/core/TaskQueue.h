#pragma once

#include <QString>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace core {

/// A unit of indexing work, produced by the UI or the filesystem watcher and
/// consumed by the indexer worker thread.
struct IndexTask {
    enum class Kind {
        AddOrUpdate,  ///< (re)index the file at `path`
        Remove,       ///< drop the file at `path` from the index
        RemoveDir,    ///< drop every indexed file under the directory `path`
        Rescan,       ///< walk the directory `path` and enqueue its files
        ReconcileDir, ///< drop indexed direct children of `path` no longer on disk
    };

    Kind kind = Kind::AddOrUpdate;
    QString path;

    bool operator==(const IndexTask&) const = default;
};

/// Thread-safe FIFO handing IndexTasks from producer threads to the indexer
/// worker.
///
/// push() never blocks and coalesces tasks already pending (a save in an
/// editor fires several change events for the same file — indexing it once is
/// enough). pop() blocks until a task arrives or stop() is called.
///
/// Shutdown: stop() wakes every blocked pop(), which then returns nullopt.
/// Pending tasks are discarded — on exit we prefer quitting fast over
/// finishing the backlog, and the persistent index catches up on next launch.
class TaskQueue {
public:
    void push(IndexTask task);

    /// Blocks until a task is available. Returns nullopt once stopped.
    std::optional<IndexTask> pop();

    void stop();
    bool isStopped() const;

    /// Number of pending tasks (the "queue depth" dashboard metric).
    int size() const;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_taskAvailable;
    std::deque<IndexTask> m_tasks;
    bool m_stopped = false;
};

} // namespace core
