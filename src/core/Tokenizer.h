#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace core {

/// Splits raw document text into normalized index terms.
///
/// Rules: contiguous runs of letters/digits form a token; everything else
/// (punctuation, whitespace, symbols) is a separator. Tokens are lowercased,
/// must be 2–64 characters long, and common English stopwords are dropped.
/// Works on QString code points, so accented/non-Latin text tokenizes correctly.
class Tokenizer {
public:
    Tokenizer() = delete; // stateless — use the static functions

    static QStringList tokenize(QStringView text);

    /// Token -> occurrence count, the shape the inverted index consumes.
    static QHash<QString, int> termFrequencies(QStringView text);

    static bool isStopword(const QString& token);

    static constexpr int kMinTokenLength = 2;
    static constexpr int kMaxTokenLength = 64;
};

} // namespace core
