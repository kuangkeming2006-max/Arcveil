#include "OverlayManager.h"
#include "OverlayManagerCodec.internal.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <tlhelp32.h>
#endif


using namespace overlay_detail;

#include "../agent/bindings/SmartHotbarPolicy.h"

namespace overlay_detail {
bool validSmartHotbarConfig(const quint32 packed) noexcept
{
    return mcoverlay::hotbar::validPacked(packed);
}

QString normalizedRgbColor(const QString &value)
{
    const QColor color(value.trimmed());
    return color.isValid() ? color.name(QColor::HexRgb).toUpper() : QString{};
}

QString decodeProtocolToken(const QByteArray &token)
{
    if (token == QByteArrayLiteral("-"))
        return {};
    return QString::fromUtf8(QByteArray::fromPercentEncoding(token));
}

QByteArray encodeProtocolToken(const QString &value)
{
    return value.isEmpty() ? QByteArrayLiteral("-")
                           : value.toUtf8().toPercentEncoding();
}

QString boundedUtf8(QString value, const qsizetype maximumBytes)
{
    // The native side stores protocol text in fixed-size UTF-8 buffers.  A
    // QString::left() character limit is insufficient because non-ASCII
    // status text may occupy several bytes per character.  Trim complete
    // Unicode characters until the encoded payload is guaranteed to fit.
    while (!value.isEmpty() && value.toUtf8().size() > maximumBytes)
        value.chop(1);
    return value;
}

}

