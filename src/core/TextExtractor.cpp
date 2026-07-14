#include "core/TextExtractor.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace core {
namespace {

const QSet<QString>& plainTextExtensions() {
    static const QSet<QString> kExtensions = {"txt", "md", "markdown", "log", "csv", "json"};
    return kExtensions;
}

} // namespace

bool TextExtractor::supports(const QString& filePath) {
    const QString ext = QFileInfo(filePath).suffix().toLower();
    return plainTextExtensions().contains(ext);
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

} // namespace core
