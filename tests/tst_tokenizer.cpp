#include <QtTest>

#include "core/Tokenizer.h"

using core::Tokenizer;

class TokenizerTest : public QObject {
    Q_OBJECT

private slots:
    void lowercasesAndSplitsOnPunctuation() {
        const auto tokens = Tokenizer::tokenize(u"Hello, World! C++ rocks?");
        QCOMPARE(tokens, QStringList({"hello", "world", "rocks"}));
    }

    void dropsStopwordsAndShortTokens() {
        const auto tokens = Tokenizer::tokenize(u"the quick brown fox is on a hill");
        QCOMPARE(tokens, QStringList({"quick", "brown", "fox", "hill"}));
    }

    void handlesUnicode() {
        const auto tokens = Tokenizer::tokenize(u"Café Zürich naïve");
        QCOMPARE(tokens, QStringList({"café", "zürich", "naïve"}));
    }

    void handlesSupplementaryPlaneCodePoints() {
        // U+20000/U+20001 are CJK Extension B ideographs: letters outside the
        // BMP, encoded as surrogate pairs in UTF-16.
        const QString cjk = QString::fromUcs4(U"\U00020000\U00020001");
        const auto tokens = Tokenizer::tokenize(QStringLiteral("before %1 after").arg(cjk));
        QCOMPARE(tokens, QStringList({"before", cjk, "after"}));

        // Emoji are symbols, not letters — they separate tokens.
        QCOMPARE(Tokenizer::tokenize(u"good\U0001F600bad"), QStringList({"good", "bad"}));
    }

    void keepsDigitsAndAlphanumerics() {
        const auto tokens = Tokenizer::tokenize(u"error 404 in utf8 parser");
        QCOMPARE(tokens, QStringList({"error", "404", "utf8", "parser"}));
    }

    void rejectsOverlongTokens() {
        const QString longToken(Tokenizer::kMaxTokenLength + 1, u'a');
        QVERIFY(Tokenizer::tokenize(longToken).isEmpty());
    }

    void emptyInputYieldsNoTokens() {
        QVERIFY(Tokenizer::tokenize(u"").isEmpty());
        QVERIFY(Tokenizer::tokenize(u" \t\n ...!!! ").isEmpty());
    }

    void countsTermFrequencies() {
        const auto freq = Tokenizer::termFrequencies(u"apple banana apple Apple banana apple");
        QCOMPARE(freq.value("apple"), 4);
        QCOMPARE(freq.value("banana"), 2);
        QCOMPARE(freq.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TokenizerTest)
#include "tst_tokenizer.moc"
