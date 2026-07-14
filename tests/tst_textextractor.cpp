#include <QtTest>

#include "core/TextExtractor.h"

using core::TextExtractor;

class TextExtractorTest : public QObject {
    Q_OBJECT

private slots:
    void supportsPlainTextFormats() {
        QVERIFY(TextExtractor::supports("notes.txt"));
        QVERIFY(TextExtractor::supports("README.md"));
        QVERIFY(TextExtractor::supports("UPPER.MD"));
        QVERIFY(!TextExtractor::supports("photo.png"));
        QVERIFY(!TextExtractor::supports("archive.zip"));
    }

    void extractsUtf8Content() {
        QTemporaryDir dir;
        const QString path = dir.filePath("sample.md");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("# Heading\n\nCafé content").toUtf8());
        file.close();

        const auto result = TextExtractor::extract(path);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains("Café content"));
    }

    void failsOnMissingFile() {
        const auto result = TextExtractor::extract("Z:/does/not/exist.txt");
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }

    void failsOnUnsupportedType() {
        const auto result = TextExtractor::extract("binary.exe");
        QVERIFY(!result.ok);
    }
};

QTEST_GUILESS_MAIN(TextExtractorTest)
#include "tst_textextractor.moc"
