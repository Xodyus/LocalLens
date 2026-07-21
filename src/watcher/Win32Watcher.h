#pragma once

// Q_OS_WIN is defined by this header, not by the compiler — it must be
// included before the #ifdef below, or that check silently (and wrongly)
// evaluates false on a fresh translation unit that hasn't pulled in any
// other Qt header yet (this file's own .cpp being the prime example).
#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QMutex>
#include <QString>
#include <QVector>

#include <atomic>
#include <thread>

#include "core/TaskQueue.h"

namespace watcher {

/// Native Windows backend: one recursive ReadDirectoryChangesW watch per
/// root directory (bWatchSubtree=TRUE), so — unlike the portable
/// FilesystemWatcher's one-QFileSystemWatcher-path-per-directory approach —
/// a thousand-subdirectory tree still costs a single OS handle.
///
/// HANDLE/OVERLAPPED are kept behind opaque pointers/an incomplete struct
/// here so <windows.h> (and its macro pollution — min/max, etc.) stays
/// confined to the .cpp.
///
/// A plain std::thread, not QThread: this class communicates purely by
/// pushing into the already thread-safe TaskQueue (same as IndexerWorker's
/// consumer side), never via Qt signals/slots, so it needs none of
/// QObject's machinery — and a bare WaitForMultipleObjects loop turned out
/// to misbehave under a QThread::run() override in this codebase's Qt Test
/// environment (confirmed via a QThread-free, Qt-free repro behaving
/// correctly), so std::thread sidesteps that entirely.
class Win32Watcher {
public:
    explicit Win32Watcher(core::TaskQueue* queue);
    /// Signals the wait loop to exit and blocks until the thread stops.
    ~Win32Watcher();

    Win32Watcher(const Win32Watcher&) = delete;
    Win32Watcher& operator=(const Win32Watcher&) = delete;

    /// Spawns the background thread. Call once.
    void start();

    /// Starts watching `rootDir` and everything beneath it.
    void watchTree(const QString& rootDir);

    /// Stops watching `rootDir`.
    void unwatchTree(const QString& rootDir);

private:
    struct Watch;

    void run();
    void openWatch(const QString& rootDir);
    void closeWatch(const QString& rootDir);
    void reissueRead(Watch& watch);
    void processEvents(Watch& watch);

    core::TaskQueue* m_queue;
    void* m_stopEvent; // HANDLE
    void* m_wakeEvent; // HANDLE
    std::thread m_thread;

    QMutex m_pendingMutex;
    QVector<QString> m_pendingRoots;
    QVector<QString> m_pendingRemovals;

    QVector<Watch*> m_watches; // touched only on the run() thread
};

} // namespace watcher

#endif // Q_OS_WIN
