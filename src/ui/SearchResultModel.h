#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "database/IndexStore.h"

namespace ui {

/// List model over BM25 search hits, exposed to QML through
/// AppController::results. Roles: path, fileName, score, matchedTerms.
class SearchResultModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS // reached via AppController's property, never created in QML

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        FileNameRole,
        ScoreRole,
        MatchedTermsRole,
        SnippetRole,
    };

    explicit SearchResultModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Replaces the whole result set (a new query supersedes the old one).
    /// `snippets` must be the same size as `hits`, aligned by index.
    void setHits(std::vector<db::SearchHit> hits, QStringList snippets);

private:
    std::vector<db::SearchHit> m_hits;
    QStringList m_snippets;
};

} // namespace ui
