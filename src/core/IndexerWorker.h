#pragma once

#include <QString>
#include <QThread>

#include "core/TaskQueue.h"

namespace core {

/// The indexing thread: drains the TaskQueue and applies each task to the
/// SQLite index (extract → tokenize → upsert), leaving the UI thread free.
///
/// Subclasses QThread (rather than the moveToThread worker pattern) because
/// the loop blocks in TaskQueue::pop() and never needs an event loop of its
/// own. Signals emitted from run() cross to the UI thread as queued
/// connections automatically.
///
/// The worker opens its own IndexStore inside run(): a QSqlDatabase
/// connection is only valid on the thread that created it.
///
/// Shutdown: call queue->stop(), then wait() — pop() returns nullopt and
/// run() falls out of the loop.
class IndexerWorker : public QThread {
    Q_OBJECT

public:
    IndexerWorker(QString databasePath, TaskQueue* queue,
                  QString connectionName = QStringLiteral("indexer"),
                  QObject* parent = nullptr);

signals:
    /// Documents or postings changed. Fires per file — receivers should
    /// coalesce (see AppController::scheduleMetricsRefresh) rather than
    /// refresh the UI on every emission.
    void indexChanged();
    void queueDepthChanged(int depth);
    void progressMessage(const QString& message);

protected:
    void run() override;

private:
    QString m_databasePath;
    QString m_connectionName;
    TaskQueue* m_queue;
};

} // namespace core
