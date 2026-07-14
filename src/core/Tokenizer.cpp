#include "core/Tokenizer.h"

#include <QSet>

namespace core {
namespace {

const QSet<QString>& stopwords() {
    static const QSet<QString> kStopwords = {
        "a",    "an",   "and",  "are", "as",   "at",   "be",   "but",  "by",   "for",
        "if",   "in",   "into", "is",  "it",   "its",  "no",   "not",  "of",   "on",
        "or",   "such", "that", "the", "their", "then", "there", "these", "they", "this",
        "to",   "was",  "will", "with"};
    return kStopwords;
}

} // namespace

bool Tokenizer::isStopword(const QString& token) {
    return stopwords().contains(token);
}

QStringList Tokenizer::tokenize(QStringView text) {
    QStringList tokens;
    QString current;
    current.reserve(16);

    auto flush = [&tokens, &current] {
        if (current.size() >= kMinTokenLength && current.size() <= kMaxTokenLength &&
            !isStopword(current)) {
            tokens.append(current);
        }
        current.clear();
    };

    for (const QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            current.append(ch.toLower());
        } else {
            flush();
        }
    }
    flush();

    return tokens;
}

QHash<QString, int> Tokenizer::termFrequencies(QStringView text) {
    QHash<QString, int> frequencies;
    for (const QString& token : tokenize(text)) {
        ++frequencies[token];
    }
    return frequencies;
}

} // namespace core
