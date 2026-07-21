#include "watcher/Win32Watcher.h"

#ifdef Q_OS_WIN

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QFileInfo>
#include <QMutexLocker>

namespace watcher {
namespace {

constexpr DWORD kBufferBytes = 64 * 1024;
constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

} // namespace

struct Win32Watcher::Watch {
    QString rootDir;
    HANDLE directoryHandle = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped{};
    std::vector<BYTE> buffer = std::vector<BYTE>(kBufferBytes);
};

Win32Watcher::Win32Watcher(core::TaskQueue* queue) : m_queue(queue) {
    m_stopEvent = CreateEventW(nullptr, /*manualReset=*/TRUE, FALSE, nullptr);
    m_wakeEvent = CreateEventW(nullptr, /*manualReset=*/TRUE, FALSE, nullptr);
}

Win32Watcher::~Win32Watcher() {
    SetEvent(static_cast<HANDLE>(m_stopEvent));
    if (m_thread.joinable())
        m_thread.join();
    CloseHandle(static_cast<HANDLE>(m_stopEvent));
    CloseHandle(static_cast<HANDLE>(m_wakeEvent));
}

void Win32Watcher::start() {
    m_thread = std::thread([this] { run(); });
}

void Win32Watcher::watchTree(const QString& rootDir) {
    {
        QMutexLocker lock(&m_pendingMutex);
        m_pendingRoots.push_back(rootDir);
    }
    // Wakes a running wait loop immediately; if run() hasn't started yet the
    // event stays signaled until it does, so no root is ever lost to timing.
    SetEvent(static_cast<HANDLE>(m_wakeEvent));
}

void Win32Watcher::reissueRead(Watch& watch) {
    ResetEvent(watch.overlapped.hEvent);
    DWORD unused = 0;
    ReadDirectoryChangesW(watch.directoryHandle, watch.buffer.data(),
                         static_cast<DWORD>(watch.buffer.size()), /*bWatchSubtree=*/TRUE,
                         kNotifyFilter, &unused, &watch.overlapped, nullptr);
}

void Win32Watcher::openWatch(const QString& rootDir) {
    const HANDLE handle = CreateFileW(
        reinterpret_cast<const wchar_t*>(rootDir.utf16()), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return; // folder vanished or is inaccessible — skip it quietly

    auto* watch = new Watch();
    watch->rootDir = rootDir;
    watch->directoryHandle = handle;
    watch->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    reissueRead(*watch);
    m_watches.push_back(watch);
}

void Win32Watcher::processEvents(Watch& watch) {
    DWORD bytesTransferred = 0;
    if (!GetOverlappedResult(watch.directoryHandle, &watch.overlapped, &bytesTransferred, FALSE)) {
        reissueRead(watch);
        return;
    }
    if (bytesTransferred == 0) {
        // The kernel couldn't fit every change into our buffer between reads.
        // We can't know what was missed, so the safe recovery is a full
        // re-scan of the tree rather than guessing.
        m_queue->push({core::IndexTask::Kind::Rescan, watch.rootDir});
        reissueRead(watch);
        return;
    }

    const BYTE* cursor = watch.buffer.data();
    for (;;) {
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
        QString relative = QString::fromWCharArray(
            info->FileName, static_cast<int>(info->FileNameLength / sizeof(WCHAR)));
        const QString absolute = watch.rootDir + QLatin1Char('/') + relative.replace('\\', '/');

        switch (info->Action) {
        case FILE_ACTION_REMOVED:
        case FILE_ACTION_RENAMED_OLD_NAME:
            // The path is already gone, so we can't stat it to tell file
            // from directory. Removing both is harmless: if it was a plain
            // file, "absolute/%" in removeDocumentsUnder matches nothing.
            m_queue->push({core::IndexTask::Kind::Remove, absolute});
            m_queue->push({core::IndexTask::Kind::RemoveDir, absolute});
            break;
        default: // FILE_ACTION_ADDED, MODIFIED, RENAMED_NEW_NAME
            if (QFileInfo(absolute).isDir())
                m_queue->push({core::IndexTask::Kind::Rescan, absolute});
            else
                m_queue->push({core::IndexTask::Kind::AddOrUpdate, absolute});
            break;
        }

        if (info->NextEntryOffset == 0)
            break;
        cursor += info->NextEntryOffset;
    }

    reissueRead(watch);
}

void Win32Watcher::run() {
    for (;;) {
        QVector<HANDLE> handles = {static_cast<HANDLE>(m_stopEvent),
                                   static_cast<HANDLE>(m_wakeEvent)};
        for (Watch* watch : m_watches)
            handles.push_back(watch->overlapped.hEvent);

        const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                     handles.data(), FALSE, INFINITE);
        if (result == WAIT_OBJECT_0)
            break; // stop requested

        if (result == WAIT_OBJECT_0 + 1) {
            ResetEvent(static_cast<HANDLE>(m_wakeEvent));
            QVector<QString> roots;
            {
                QMutexLocker lock(&m_pendingMutex);
                roots.swap(m_pendingRoots);
            }
            for (const QString& root : roots)
                openWatch(root);
            continue;
        }

        const int index = static_cast<int>(result - WAIT_OBJECT_0 - 2);
        if (index >= 0 && index < m_watches.size())
            processEvents(*m_watches[index]);
    }

    for (Watch* watch : m_watches) {
        CancelIo(watch->directoryHandle);
        CloseHandle(watch->overlapped.hEvent);
        CloseHandle(watch->directoryHandle);
        delete watch;
    }
    m_watches.clear();
}

} // namespace watcher

#endif // Q_OS_WIN
