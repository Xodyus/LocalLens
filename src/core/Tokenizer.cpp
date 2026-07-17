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

    // Iterate code points, not UTF-16 code units: characters outside the BMP
    // (e.g. CJK Extension B ideographs) arrive as surrogate pairs, and QChar's
    // per-unit classification would silently drop them.
    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size; ++i) {
        char32_t codePoint = text[i].unicode();
        if (QChar::isHighSurrogate(codePoint) && i + 1 < size && text[i + 1].isLowSurrogate()) {
            ++i;
            codePoint = QChar::surrogateToUcs4(static_cast<char16_t>(codePoint),
                                               text[i].unicode());
        }
        if (QChar::isLetterOrNumber(codePoint)) {
            current.append(QChar::fromUcs4(QChar::toLower(codePoint)));
        } else {
            flush(); // includes unpaired surrogates, which classify as symbols
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
