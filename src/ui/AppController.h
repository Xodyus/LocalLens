#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "core/IndexerWorker.h"
#include "core/TaskQueue.h"
#include "database/IndexStore.h"
#include "ui/SearchResultModel.h"

#ifdef Q_OS_WIN
#include "watcher/Win32Watcher.h"
#else
#include "watcher/FilesystemWatcher.h"
#endif

namespace ui {

#ifdef Q_OS_WIN
using ActiveWatcher = watcher::Win32Watcher;
#else
using ActiveWatcher = watcher::FilesystemWatcher;
#endif

/// Bridges the C++ backend to the QML dashboard.
///
/// Threading: the UI thread owns this object and the watcher; all indexing
/// happens on the IndexerWorker thread, fed through the TaskQueue. Worker
/// signals arrive here as queued connections. search() itself runs on the Qt
/// thread pool via QtConcurrent — each call opens its own short-lived
/// IndexStore connection (QSqlDatabase connections are thread-bound, and
/// pool threads are reused across calls, so a persistent connection can't be
/// pinned to "the search thread"). A generation counter, checked in
/// onSearchFinished(), discards a slow older query's results if a newer
/// query has already returned.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Live metrics for the dashboard tiles; metricsChanged re-evaluates them.
    Q_PROPERTY(qint64 documentCount READ documentCount NOTIFY metricsChanged)
    Q_PROPERTY(qint64 termCount READ termCount NOTIFY metricsChanged)
    Q_PROPERTY(qint64 databaseSizeBytes READ databaseSizeBytes NOTIFY metricsChanged)
    /// Pending tasks on the indexer queue (0 when idle).
    Q_PROPERTY(int queueDepth READ queueDepth NOTIFY queueDepthChanged)

    /// Folders currently indexed + watched, persisted across restarts.
    Q_PROPERTY(QStringList watchedFolders READ watchedFolders NOTIFY watchedFoldersChanged)

    // Search state.
    Q_PROPERTY(ui::SearchResultModel* results READ results CONSTANT)
    Q_PROPERTY(double lastQueryMs READ lastQueryMs NOTIFY searchFinished)

    /// Human-readable status line (index location, errors, last operation).
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    qint64 documentCount() const { return m_store.documentCount(); }
    qint64 termCount() const { return m_store.termCount(); }
    qint64 databaseSizeBytes() const { return m_store.databaseSizeBytes(); }
    int queueDepth() const { return m_queueDepth; }
    QStringList watchedFolders() const { return m_watchedFolders; }
    SearchResultModel* results() const { return m_results; }
    double lastQueryMs() const { return m_lastQueryMs; }
    QString status() const { return m_status; }

    /// Tokenizes the raw query and kicks off an asynchronous BM25 search;
    /// results land in the `results` model once onSearchFinished() runs.
    Q_INVOKABLE void search(const QString& query);

    /// Queues a recursive index of `folder` (a file:// URL from FolderDialog)
    /// on the worker thread and starts watching it for changes. Persisted,
    /// so it's restored and re-scanned on the next launch.
    Q_INVOKABLE void indexFolder(const QUrl& folder);

    /// Stops watching `folder` and removes its documents from the index.
    Q_INVOKABLE void removeFolder(const QString& folder);

    /// Autocomplete: index terms starting with `prefix`, most widespread
    /// first. Synchronous — a LIKE 'x%' on an indexed column is fast.
    Q_INVOKABLE QStringList suggest(const QString& prefix);

    /// Opens `path`'s containing folder in the OS file browser with the file
    /// pre-selected (Windows Explorer's "/select," convention).
    Q_INVOKABLE void revealInExplorer(const QString& path) const;

signals:
    void metricsChanged();
    void queueDepthChanged();
    void watchedFoldersChanged();
    void searchFinished();
    void statusChanged();

private slots:
    void onSearchFinished();

private:
    /// Result of one background search(); generation lets a stale reply be
    /// dropped if a newer query has already completed.
    struct SearchOutcome {
        std::vector<db::SearchHit> hits;
        QStringList snippets; // aligned with hits by index
        double queryMs = 0.0;
        quint64 generation = 0;
    };

    void addWatchedFolder(const QString& root);
    void saveWatchedFolders();
    void scheduleMetricsRefresh();
    void setStatus(const QString& status);

    QString m_databasePath;
    db::IndexStore m_store;
    core::TaskQueue m_queue;
    core::IndexerWorker* m_worker = nullptr;
    ActiveWatcher* m_watcher = nullptr;
    QStringList m_watchedFolders;
    SearchResultModel* m_results = nullptr;
    QFutureWatcher<SearchOutcome> m_searchWatcher;
    quint64 m_searchGeneration = 0;
    double m_lastQueryMs = 0.0;
    int m_queueDepth = 0;
    QString m_status;
    bool m_refreshPending = false;
};

} // namespace ui
