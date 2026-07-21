#include "ui/AppController.h"

#include <QAtomicInt>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrentRun>

#include "core/TextExtractor.h"
#include "core/Tokenizer.h"

namespace ui {
namespace {

const QString kWatchedFoldersKey = QStringLiteral("watchedFolders");

QString databaseFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/index.db");
}

/// A short excerpt around the first matched term, for the results list.
/// Re-reads the file from disk — acceptable here because this runs on the
/// search's own background thread (see AppController::search), not the UI
/// thread, and only for the page of results actually returned.
QString buildSnippet(const QString& path, const QStringList& terms) {
    const core::TextExtractor::Result extracted = core::TextExtractor::extract(path);
    if (!extracted.ok)
        return QString();
    const QString& text = extracted.text;

    int matchPos = -1;
    for (const QString& term : terms) {
        const int pos = text.indexOf(term, 0, Qt::CaseInsensitive);
        if (pos >= 0 && (matchPos < 0 || pos < matchPos))
            matchPos = pos;
    }

    constexpr int kWindow = 160;
    constexpr int kLeadIn = 40;
    const int start = matchPos < 0 ? 0 : qMax(0, matchPos - kLeadIn);
    const int length = qMin(kWindow, text.size() - start);
    QString snippet = text.mid(start, length).simplified();
    if (start > 0)
        snippet.prepend(QStringLiteral("…"));
    if (start + length < text.size())
        snippet.append(QStringLiteral("…"));
    return snippet;
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

    // Restore folders indexed in a previous session and re-scan them — the
    // index itself persists, but watches don't survive a restart otherwise.
    const QStringList saved = QSettings().value(kWatchedFoldersKey).toStringList();
    for (const QString& folder : saved) {
        if (QFileInfo(folder).isDir())
            addWatchedFolder(folder);
    }

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

        outcome.snippets.reserve(static_cast<qsizetype>(outcome.hits.size()));
        for (const db::SearchHit& hit : outcome.hits)
            outcome.snippets.append(buildSnippet(hit.path, terms));
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
    m_results->setHits(std::move(outcome.hits), std::move(outcome.snippets));
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
    addWatchedFolder(root);
}

void AppController::addWatchedFolder(const QString& root) {
    // Stored paths use '/' separators (see IndexStore's convention); match it
    // so removeFolder()'s exact-string checks against m_watchedFolders work.
    const QString normalized = QDir::fromNativeSeparators(root);
    if (m_watchedFolders.contains(normalized)) {
        setStatus(QStringLiteral("Already watching %1").arg(QDir::toNativeSeparators(normalized)));
        return;
    }

    m_watcher->watchTree(normalized);
    m_queue.push({core::IndexTask::Kind::Rescan, normalized});

    m_watchedFolders.append(normalized);
    saveWatchedFolders();
    emit watchedFoldersChanged();
    setStatus(QStringLiteral("Watching %1").arg(QDir::toNativeSeparators(normalized)));
}

void AppController::removeFolder(const QString& folder) {
    if (!m_watchedFolders.removeOne(folder))
        return;
    saveWatchedFolders();
    emit watchedFoldersChanged();

    m_watcher->unwatchTree(folder);
    m_queue.push({core::IndexTask::Kind::RemoveDir, folder});
    setStatus(QStringLiteral("Removed %1").arg(QDir::toNativeSeparators(folder)));
}

void AppController::saveWatchedFolders() {
    QSettings settings;
    settings.setValue(kWatchedFoldersKey, m_watchedFolders);
}

QStringList AppController::suggest(const QString& prefix) {
    return m_store.suggestTerms(prefix.trimmed());
}

void AppController::revealInExplorer(const QString& path) const {
    // A single argument containing "/select," immediately followed by the
    // (comma-free) path is the syntax Explorer expects; passing them as two
    // separate QProcess arguments inserts a space Explorer won't accept.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
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
