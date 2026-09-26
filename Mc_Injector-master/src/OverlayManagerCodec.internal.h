#pragma once

#include <QByteArray>
#include <QString>

namespace overlay_detail {
bool validSmartHotbarConfig(const quint32 packed) noexcept;
QString normalizedRgbColor(const QString &value);
QString decodeProtocolToken(const QByteArray &token);
QByteArray encodeProtocolToken(const QString &value);
QString boundedUtf8(QString value, const qsizetype maximumBytes);
bool validConfigName(const QString &name);
}
