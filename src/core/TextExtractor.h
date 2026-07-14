#pragma once

#include <QString>

namespace core {

/// Turns a file on disk into plain text ready for tokenization.
///
/// Plain-text formats (.txt, .md, .log, ...) are read directly as UTF-8.
/// PDF support plugs in here later without touching the indexing pipeline.
class TextExtractor {
public:
    struct Result {
        bool ok = false;
        QString text;
        QString error;
    };

    TextExtractor() = delete;

    /// True if the file extension is one we know how to index.
    static bool supports(const QString& filePath);

    static Result extract(const QString& filePath);

    /// Files larger than this are skipped to keep indexing latency bounded.
    static constexpr qint64 kMaxFileSizeBytes = 16 * 1024 * 1024;
};

} // namespace core
