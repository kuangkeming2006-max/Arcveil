#pragma once

#include <QByteArray>
#include <QString>
#include <algorithm>

// Keys are opaque credentials, not UUIDs. Personal/production credentials
// must reach the same API-Key header without rewriting or guessing a prefix.
inline QByteArray normalizeHypixelApiKey(const QString& text)
{
    const QByteArray key = text.trimmed().toUtf8();
    if (key.isEmpty() || key.size() > 8192 ||
        std::any_of(key.begin(), key.end(), [](unsigned char ch) {
            return ch <= 0x20 || ch >= 0x7f;
        })) return {};
    return key;
}
