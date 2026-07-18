#include "core/IndexerWorker.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "core/TextExtractor.h"
#include "core/Tokenizer.h"
#include "database/IndexStore.h"

namespace core {
namespace {

/// Indexes (or removes) one file. Returns true if the index changed.
bool processFile(db::IndexStore& store, const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        // The file vanished between the event and now — treat as a removal.
        return store.removeDocument(path);
    }
    const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();
    if (!store.needsReindex(path, mtimeMs, info.size()))
        return false;
    const TextExtractor::Result extracted = TextExtractor::extract(path);
    if (!extracted.ok)
        return false; // unsupported/oversized/unreadable — skip quietly
    return store.upsertDocument(path, mtimeMs, info.size(),
                                Tokenizer::termFrequencies(extracted.text));
}

} // namespace

IndexerWorker::IndexerWorker(QString databasePath, TaskQueue* queue, QString connectionName,
                             QObject* parent)
    : QThread(parent),
      m_databasePath(std::move(databasePath)),
      m_connectionName(std::move(connectionName)),
      m_queue(queue) {}

void IndexerWorker::run() {
    db::IndexStore store(m_databasePath, m_connectionName);
    if (!store.open()) {
        emit progressMessage(tr("Indexer failed to open database: %1").arg(store.lastError()));
        return;
    }

    while (const std::optional<IndexTask> task = m_queue->pop()) {
        bool changed = false;
        switch (task->kind) {
        case IndexTask::Kind::Rescan: {
            // Expand the walk into one task per file so queue depth reflects
            // real pending work (and duplicate events coalesce per file).
            int enqueued = 0;
            QDirIterator it(task->path, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                if (TextExtractor::supports(path)) {
                    m_queue->push({IndexTask::Kind::AddOrUpdate, path});
                    ++enqueued;
                }
            }
            if (enqueued > 0)
                emit progressMessage(tr("Scanning %1 — queued %2 file(s)")
                                         .arg(QDir::toNativeSeparators(task->path))
                                         .arg(enqueued));
            break;
        }
        case IndexTask::Kind::AddOrUpdate:
            changed = processFile(store, task->path);
            break;
        case IndexTask::Kind::Remove:
            changed = store.removeDocument(task->path);
            break;
        case IndexTask::Kind::RemoveDir:
            changed = store.removeDocumentsUnder(task->path);
            break;
        }
        if (changed)
            emit indexChanged();
        emit queueDepthChanged(m_queue->size());
    }
}

} // namespace core
