#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "core/IndexerWorker.h"
#include "core/TaskQueue.h"
#include "database/IndexStore.h"
#include "ui/SearchResultModel.h"
#include "watcher/FilesystemWatcher.h"

namespace ui {

/// Bridges the C++ backend to the QML dashboard.
///
/// Threading: the UI thread owns this object, its read connection
/// (m_store), and the watcher; all indexing happens on the IndexerWorker
/// thread, fed through the TaskQueue. Worker signals arrive here as queued
/// connections. Search still runs synchronously on the UI thread —
/// acceptable while queries are sub-millisecond.
///
/// later: async search — run m_store.search on the Qt thread pool
/// (QtConcurrent::run) with a second read-only IndexStore connection, and
/// tag each query with a generation counter so a slow older query can't
/// overwrite a newer one's results.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Live metrics for the dashboard tiles; metricsChanged re-evaluates them.
    Q_PROPERTY(qint64 documentCount READ documentCount NOTIFY metricsChanged)
    Q_PROPERTY(qint64 termCount READ termCount NOTIFY metricsChanged)
    Q_PROPERTY(qint64 databaseSizeBytes READ databaseSizeBytes NOTIFY metricsChanged)
    /// Pending tasks on the indexer queue (0 when idle).
    Q_PROPERTY(int queueDepth READ queueDepth NOTIFY queueDepthChanged)

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
    SearchResultModel* results() const { return m_results; }
    double lastQueryMs() const { return m_lastQueryMs; }
    QString status() const { return m_status; }

    /// Tokenizes the raw query, runs a BM25 search, and publishes the results.
    Q_INVOKABLE void search(const QString& query);

    /// Queues a recursive index of `folder` (a file:// URL from FolderDialog)
    /// on the worker thread and starts watching it for changes.
    ///
    /// later: folder management — keep a watchedFolders list property,
    /// persist it with QSettings, restore + re-scan on startup, and add
    /// removeFolder() that pushes a RemoveDir task and stops the watch.
    Q_INVOKABLE void indexFolder(const QUrl& folder);

    /// Autocomplete: index terms starting with `prefix`, most widespread
    /// first. Synchronous — a LIKE 'x%' on an indexed column is fast.
    Q_INVOKABLE QStringList suggest(const QString& prefix);

signals:
    void metricsChanged();
    void queueDepthChanged();
    void searchFinished();
    void statusChanged();

private:
    void scheduleMetricsRefresh();
    void setStatus(const QString& status);

    db::IndexStore m_store;
    core::TaskQueue m_queue;
    core::IndexerWorker* m_worker = nullptr;
    watcher::FilesystemWatcher* m_watcher = nullptr;
    SearchResultModel* m_results = nullptr;
    double m_lastQueryMs = 0.0;
    int m_queueDepth = 0;
    QString m_status;
    bool m_refreshPending = false;
};

} // namespace ui
