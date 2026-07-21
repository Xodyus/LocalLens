#include "ui/AppController.h"

#include <QAtomicInt>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrentRun>

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
      m_databasePath(databaseFilePath()),
      m_store(m_databasePath, QStringLiteral("ui-main")),
      m_results(new SearchResultModel(this)) {
    if (m_store.open())
        setStatus(QStringLiteral("Index ready at %1").arg(m_databasePath));
    else
        setStatus(QStringLiteral("Failed to open index: %1").arg(m_store.lastError()));

    m_worker = new core::IndexerWorker(m_databasePath, &m_queue, QStringLiteral("indexer"), this);
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

    // Not QObject-parented: Win32Watcher isn't a QObject (see its header for
    // why), so both backends are owned and deleted manually for symmetry.
    m_watcher = new ActiveWatcher(&m_queue);
#ifdef Q_OS_WIN
    m_watcher->start();
#endif

    connect(&m_searchWatcher, &QFutureWatcherBase::finished, this,
            &AppController::onSearchFinished);

    emit metricsChanged();
}

AppController::~AppController() {
    // Wake the worker out of pop() and let it exit before the queue (a
    // member, destroyed with us) goes away.
    m_queue.stop();
    m_worker->wait(5000);
    // m_watcher isn't QObject-parented (see its construction), so it isn't
    // deleted automatically; its own destructor stops its background thread.
    delete m_watcher;
}

void AppController::search(const QString& query) {
    const QStringList terms = core::Tokenizer::tokenize(query);
    const quint64 generation = ++m_searchGeneration;
    const QString dbPath = m_databasePath;

    QFuture<SearchOutcome> future = QtConcurrent::run([dbPath, terms, generation]() {
        SearchOutcome outcome;
        outcome.generation = generation;

        // Pool threads are reused across calls, so a persistent connection
        // can't be pinned to "the search thread" — each call opens (and
        // closes, via IndexStore's destructor) its own, uniquely named so
        // concurrent searches on different pool threads don't collide.
        static QAtomicInt connectionCounter;
        const QString connectionName =
            QStringLiteral("search-%1").arg(connectionCounter.fetchAndAddRelaxed(1));
        db::IndexStore store(dbPath, connectionName);
        if (!store.open())
            return outcome;

        QElapsedTimer timer;
        timer.start();
        outcome.hits = store.search(terms);
        outcome.queryMs = timer.nsecsElapsed() / 1e6;
        return outcome;
    });

    m_searchWatcher.setFuture(future);
}

void AppController::onSearchFinished() {
    SearchOutcome outcome = m_searchWatcher.result();
    if (outcome.generation != m_searchGeneration)
        return; // superseded by a newer query — drop this stale reply

    const auto hitCount = outcome.hits.size();
    m_lastQueryMs = outcome.queryMs;
    m_results->setHits(std::move(outcome.hits));
    emit searchFinished();

    setStatus(
        QStringLiteral("%1 hit(s) in %2 ms").arg(hitCount).arg(m_lastQueryMs, 0, 'f', 2));
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
