#include "core/TextExtractor.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <poppler-qt6.h>

namespace core {
namespace {

const QSet<QString>& plainTextExtensions() {
    static const QSet<QString> kExtensions = {"txt", "md", "markdown", "log", "csv", "json"};
    return kExtensions;
}

TextExtractor::Result extractPlainText(const QString& filePath) {
    TextExtractor::Result result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    // fromUtf8 substitutes U+FFFD for invalid sequences, so files in other
    // encodings degrade gracefully instead of aborting the whole document.
    result.text = QString::fromUtf8(file.readAll());
    result.ok = true;
    return result;
}

TextExtractor::Result extractPdfText(const QString& filePath) {
    TextExtractor::Result result;

    const std::unique_ptr<Poppler::Document> document = Poppler::Document::load(filePath);
    if (!document) {
        result.error = QStringLiteral("could not parse PDF");
        return result;
    }
    if (document->isLocked()) {
        result.error = QStringLiteral("PDF is password-protected");
        return result;
    }

    QString text;
    for (int i = 0; i < document->numPages(); ++i) {
        const std::unique_ptr<Poppler::Page> page = document->page(i);
        if (!page)
            continue; // a single corrupt page shouldn't sink the whole document
        // A null rect means "whole page"; a trailing newline keeps the last
        // word of one page from fusing with the first word of the next.
        text += page->text(QRectF());
        text += QChar(u'\n');
    }

    result.text = text;
    result.ok = true;
    return result;
}

} // namespace

bool TextExtractor::supports(const QString& filePath) {
    const QString ext = QFileInfo(filePath).suffix().toLower();
    return ext == QLatin1String("pdf") || plainTextExtensions().contains(ext);
}

TextExtractor::Result TextExtractor::extract(const QString& filePath) {
    Result result;

    const QFileInfo info(filePath);
    if (!supports(filePath)) {
        result.error = QStringLiteral("unsupported file type: %1").arg(info.suffix());
        return result;
    }
    if (info.size() > kMaxFileSizeBytes) {
        result.error = QStringLiteral("file exceeds %1 MB limit")
                           .arg(kMaxFileSizeBytes / (1024 * 1024));
        return result;
    }

    if (info.suffix().toLower() == QLatin1String("pdf"))
        return extractPdfText(filePath);
    return extractPlainText(filePath);
}

} // namespace core
