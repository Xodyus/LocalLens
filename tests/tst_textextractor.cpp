#include <QtTest>

#include "core/TextExtractor.h"

using core::TextExtractor;

namespace {

/// Hand-assembles the smallest PDF Poppler will parse: one page, one
/// Helvetica text run, a manual xref table. Building it in code (rather than
/// shipping a binary fixture) keeps the test self-contained and lets us
/// compute the byte offsets the xref table requires as we go.
QByteArray buildMinimalPdf(const QByteArray& text) {
    const QByteArray content = "BT /F1 24 Tf 20 100 Td (" + text + ") Tj ET";

    QByteArray pdf = "%PDF-1.4\n";
    QVector<int> offsets(6, 0); // index 0 unused, objects are numbered 1..5

    auto appendObject = [&](int number, const QByteArray& body) {
        offsets[number] = pdf.size();
        pdf += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
    };

    appendObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    appendObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    appendObject(3,
                "<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 4 0 R >> >> "
                "/MediaBox [0 0 200 200] /Contents 5 0 R >>");
    appendObject(4, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    appendObject(5,
                "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
                    "\nendstream");

    const int xrefOffset = pdf.size();
    pdf += "xref\n0 6\n0000000000 65535 f \n";
    for (int i = 1; i <= 5; ++i)
        pdf += QByteArray::number(offsets[i]).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefOffset) +
           "\n%%EOF";
    return pdf;
}

} // namespace

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

    void supportsPdf() {
        QVERIFY(TextExtractor::supports("report.pdf"));
        QVERIFY(TextExtractor::supports("REPORT.PDF"));
    }

    void extractsTextFromPdf() {
        QTemporaryDir dir;
        const QString path = dir.filePath("sample.pdf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(buildMinimalPdf("hello pdf world"));
        file.close();

        const auto result = TextExtractor::extract(path);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.text.contains("hello"));
        QVERIFY(result.text.contains("world"));
    }

    void failsOnCorruptPdf() {
        QTemporaryDir dir;
        const QString path = dir.filePath("broken.pdf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.4\nnot actually a pdf");
        file.close();

        const auto result = TextExtractor::extract(path);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
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
