#include "ui/AppController.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

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
    : QObject(parent),
      m_store(databaseFilePath(), QStringLiteral("ui-main")),
      m_results(new SearchResultModel(this)) {
    if (m_store.open())
        setStatus(QStringLiteral("Index ready at %1").arg(databaseFilePath()));
    else
        setStatus(QStringLiteral("Failed to open index: %1").arg(m_store.lastError()));

    m_worker = new core::IndexerWorker(databaseFilePath(), &m_queue,
                                       QStringLiteral("indexer"), this);
    connect(m_worker, &core::IndexerWorker::indexChanged, this,
            &AppController::scheduleMetricsRefresh);
    connect(m_worker, &core::IndexerWorker::progressMessage, this, &AppController::setStatus);
    connect(m_worker, &core::IndexerWorker::queueDepthChanged, this, [this](int depth) {
        if (m_queueDepth != depth) {
            m_queueDepth = depth;
            emit queueDepthChanged();
        }
    });
    m_worker->start();

    m_watcher = new watcher::FilesystemWatcher(&m_queue, this);

    emit metricsChanged();
}

AppController::~AppController() {
    // Wake the worker out of pop() and let it exit before the queue (a
    // member, destroyed with us) goes away.
    m_queue.stop();
    m_worker->wait(5000);
}

void AppController::search(const QString& query) {
    const QStringList terms = core::Tokenizer::tokenize(query);

    QElapsedTimer timer;
    timer.start();
    std::vector<db::SearchHit> hits = m_store.search(terms);
    m_lastQueryMs = timer.nsecsElapsed() / 1e6;

    const auto hitCount = hits.size();
    m_results->setHits(std::move(hits));
    emit searchFinished();

    if (!terms.isEmpty())
        setStatus(QStringLiteral("%1 hit(s) in %2 ms")
                      .arg(hitCount)
                      .arg(m_lastQueryMs, 0, 'f', 2));
}

void AppController::indexFolder(const QUrl& folder) {
    const QString root = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        setStatus(QStringLiteral("Not a folder: %1").arg(folder.toString()));
        return;
    }

    m_watcher->watchTree(root);
    m_queue.push({core::IndexTask::Kind::Rescan, root});
    setStatus(QStringLiteral("Watching %1").arg(QDir::toNativeSeparators(root)));
}

QStringList AppController::suggest(const QString& prefix) {
    return m_store.suggestTerms(prefix.trimmed());
}

void AppController::scheduleMetricsRefresh() {
    // The worker fires indexChanged per file; refreshing the UI at most every
    // 200 ms keeps thousands of rapid signals from thrashing the bindings.
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(200, this, [this] {
        m_refreshPending = false;
        emit metricsChanged();
    });
}

void AppController::setStatus(const QString& status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

} // namespace ui
