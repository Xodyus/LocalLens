#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

#include "core/TaskQueue.h"

namespace watcher {

/// Watches folder trees and converts filesystem change events into index
/// tasks. Lives on the UI thread (QFileSystemWatcher needs an event loop);
/// the work itself happens on the indexer thread via the queue.
///
/// Portable backend on QFileSystemWatcher: directories are watched (on
/// Windows this fires on file writes inside the directory too), and any
/// change enqueues a Rescan of that directory — needsReindex() makes rescans
/// of unchanged files cheap, and the queue coalesces duplicate pending
/// rescans, which doubles as the debounce for editors that fire several
/// events per save.
///
/// On Windows, AppController uses Win32Watcher instead (native
/// ReadDirectoryChangesW, one handle per watched root regardless of
/// subdirectory count); this class remains the portable fallback for other
/// platforms, behind the same constructor + watchTree() contract.
class FilesystemWatcher : public QObject {
    Q_OBJECT

public:
    explicit FilesystemWatcher(core::TaskQueue* queue, QObject* parent = nullptr);

    /// Starts watching `rootDir` and every subdirectory beneath it.
    void watchTree(const QString& rootDir);

    /// Stops watching `rootDir` and everything beneath it.
    void unwatchTree(const QString& rootDir);

private:
    void onDirectoryChanged(const QString& dir);
    void addDirectory(const QString& dir);

    QFileSystemWatcher m_watcher;
    core::TaskQueue* m_queue;
};

} // namespace watcher
