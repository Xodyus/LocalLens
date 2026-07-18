#include "watcher/FilesystemWatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace watcher {

FilesystemWatcher::FilesystemWatcher(core::TaskQueue* queue, QObject* parent)
    : QObject(parent), m_queue(queue) {
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &FilesystemWatcher::onDirectoryChanged);
}

void FilesystemWatcher::watchTree(const QString& rootDir) {
    addDirectory(rootDir);
    QDirIterator it(rootDir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        addDirectory(it.next());
}

void FilesystemWatcher::addDirectory(const QString& dir) {
    if (!m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
}

void FilesystemWatcher::onDirectoryChanged(const QString& dir) {
    if (!QFileInfo::exists(dir)) {
        // The directory itself was deleted or renamed away.
        m_watcher.removePath(dir);
        m_queue->push({core::IndexTask::Kind::RemoveDir, dir});
        return;
    }

    // New subdirectories created since we started watching need watches too;
    // addDirectory() dedupes the ones we already track.
    watchTree(dir);
    m_queue->push({core::IndexTask::Kind::Rescan, dir});

    // later: deletion reconciliation — a file deleted from `dir` stays in
    // the index (Rescan only visits files that exist). Add something like
    // IndexStore::documentPathsUnder(prefix), diff it against the walk, and
    // push Remove tasks for paths that are gone.
}

} // namespace watcher
