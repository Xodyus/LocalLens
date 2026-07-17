#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "database/IndexStore.h"

namespace ui {

/// Bridges the C++ backend to the QML dashboard.
///
/// Scaffold note: everything here runs on the UI thread, including
/// indexFolder(), which will freeze the window on large folders. The target
/// architecture (see README) moves indexing to a worker thread and search to
/// the Qt thread pool — that migration is a later exercise.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Live metrics for the dashboard cards. All three share one NOTIFY signal:
    // emit metricsChanged() after any operation that touches the index and
    // every bound QML expression re-evaluates.
    Q_PROPERTY(qint64 documentCount READ documentCount NOTIFY metricsChanged)
    Q_PROPERTY(qint64 termCount READ termCount NOTIFY metricsChanged)
    Q_PROPERTY(qint64 databaseSizeBytes READ databaseSizeBytes NOTIFY metricsChanged)

    // Search state.
    // TODO(you): replace `results` with a QAbstractListModel so the view can
    // show score / matchedTerms per hit, not just the path (learning step 2).
    Q_PROPERTY(QStringList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(double lastQueryMs READ lastQueryMs NOTIFY resultsChanged)

    /// Human-readable status line (index location, errors, last operation).
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit AppController(QObject* parent = nullptr);

    qint64 documentCount() const { return m_store.documentCount(); }
    qint64 termCount() const { return m_store.termCount(); }
    qint64 databaseSizeBytes() const { return m_store.databaseSizeBytes(); }
    QStringList results() const { return m_results; }
    double lastQueryMs() const { return m_lastQueryMs; }
    QString status() const { return m_status; }

    /// Tokenizes the raw query, runs a BM25 search, and publishes the results.
    Q_INVOKABLE void search(const QString& query);

    /// Recursively indexes every supported file under `folder` (a file:// URL
    /// from FolderDialog). Synchronous for now — see class note.
    Q_INVOKABLE void indexFolder(const QUrl& folder);

    // TODO(you): Q_INVOKABLE QStringList suggest(const QString& prefix) for
    // type-ahead, backed by IndexStore::suggestTerms() (learning step 4).

signals:
    void metricsChanged();
    void resultsChanged();
    void statusChanged();

private:
    void setStatus(const QString& status);

    db::IndexStore m_store;
    QStringList m_results;
    double m_lastQueryMs = 0.0;
    QString m_status;
};

} // namespace ui
