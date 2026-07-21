#include "ui/SearchResultModel.h"

#include <QFileInfo>

namespace ui {

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_hits.size());
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const db::SearchHit& hit = m_hits[static_cast<size_t>(index.row())];
    switch (role) {
    case PathRole:
        return hit.path;
    case FileNameRole:
        return QFileInfo(hit.path).fileName();
    case ScoreRole:
        return hit.score;
    case MatchedTermsRole:
        return hit.matchedTerms;
    case SnippetRole:
        return index.row() < m_snippets.size() ? m_snippets.at(index.row()) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> SearchResultModel::roleNames() const {
    return {
        {PathRole, "path"},
        {FileNameRole, "fileName"},
        {ScoreRole, "score"},
        {MatchedTermsRole, "matchedTerms"},
        {SnippetRole, "snippet"},
    };
}

void SearchResultModel::setHits(std::vector<db::SearchHit> hits, QStringList snippets) {
    beginResetModel();
    m_hits = std::move(hits);
    m_snippets = std::move(snippets);
    endResetModel();
}

} // namespace ui
