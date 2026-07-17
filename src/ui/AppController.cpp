#include "ui/AppController.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>

#include "core/TextExtractor.h"
#include "core/Tokenizer.h"

namespace ui {
namespace {

QString databaseFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/index.db");
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent), m_store(databaseFilePath(), QStringLiteral("ui-main")) {
    if (m_store.open())
        setStatus(QStringLiteral("Index ready at %1").arg(databaseFilePath()));
    else
        setStatus(QStringLiteral("Failed to open index: %1").arg(m_store.lastError()));
    emit metricsChanged();
}

void AppController::search(const QString& query) {
    const QStringList terms = core::Tokenizer::tokenize(query);

    QElapsedTimer timer;
    timer.start();
    const std::vector<db::SearchHit> hits = m_store.search(terms);
    m_lastQueryMs = timer.nsecsElapsed() / 1e6;

    m_results.clear();
    m_results.reserve(static_cast<qsizetype>(hits.size()));
    for (const db::SearchHit& hit : hits)
        m_results.append(hit.path);
    emit resultsChanged();

    setStatus(QStringLiteral("%1 hit(s) in %2 ms")
                  .arg(hits.size())
                  .arg(m_lastQueryMs, 0, 'f', 2));
}

void AppController::indexFolder(const QUrl& folder) {
    const QString root = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        setStatus(QStringLiteral("Not a folder: %1").arg(folder.toString()));
        return;
    }

    int indexed = 0;
    int unchanged = 0;
    int failed = 0;

    // QDirIterator yields '/'-separated paths on every platform, which keeps
    // stored paths consistent with IndexStore::removeDocumentsUnder().
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!core::TextExtractor::supports(path))
            continue;

        const QFileInfo info(path);
        const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();
        if (!m_store.needsReindex(path, mtimeMs, info.size())) {
            ++unchanged;
            continue;
        }

        const core::TextExtractor::Result extracted = core::TextExtractor::extract(path);
        if (!extracted.ok) {
            ++failed;
            continue;
        }

        if (m_store.upsertDocument(path, mtimeMs, info.size(),
                                   core::Tokenizer::termFrequencies(extracted.text)))
            ++indexed;
        else
            ++failed;
    }

    emit metricsChanged();
    setStatus(QStringLiteral("Indexed %1 file(s) under %2 (%3 unchanged, %4 failed)")
                  .arg(indexed)
                  .arg(root)
                  .arg(unchanged)
                  .arg(failed));
}

void AppController::setStatus(const QString& status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

} // namespace ui
