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

void OverlayManager::publishHypixelResult(
    const int state, const QString &uuid, const QString &displayName,
    const qint64 wins, const qint64 losses, const qint64 finalKills,
    const qint64 finalDeaths, const qint64 bedsBroken, const qint64 bedsLost,
    const double winRate, const double fkdr, const QString &status)
{
    if (!m_authenticated) return;
    const QString safeUuid = boundedUtf8(uuid.trimmed(), 38);
    const QString safeDisplayName = boundedUtf8(displayName.trimmed(), 46);
    const QString safeStatus = boundedUtf8(status.simplified(), 158);
    QByteArray command = QByteArrayLiteral("HYPIXEL_RESULT ")
        + QByteArray::number(std::clamp(state, 0, 3)) + ' '
        + encodeProtocolToken(safeUuid) + ' ' + encodeProtocolToken(safeDisplayName) + ' '
        + QByteArray::number(wins) + ' ' + QByteArray::number(losses) + ' '
        + QByteArray::number(finalKills) + ' ' + QByteArray::number(finalDeaths) + ' '
        + QByteArray::number(bedsBroken) + ' ' + QByteArray::number(bedsLost) + ' '
        + QByteArray::number(std::isfinite(winRate) ? winRate : 0.0, 'g', 9) + ' '
        + QByteArray::number(std::isfinite(fkdr) ? fkdr : 0.0, 'g', 9) + ' '
        + encodeProtocolToken(safeStatus) + '\n';
    writeAgentCommand(command);
}

void OverlayManager::publishPlayerStats(const QString &playerName,
                                        const QString &teamPrefix,
                                        const int stars,
                                        const double fkdr,
                                        const double wlr,
                                        const double bblr,
                                        const qint64 wins,
                                        const qint64 finalKills,
                                        const qint64 bedsBroken,
                                        const int winStreak,
                                        const int level)
{
    if (!m_authenticated) return;
    static const QRegularExpression nameExpression(
        QStringLiteral("^[A-Za-z0-9_]{1,16}$"));
    const QString team = teamPrefix.toLower();
    const bool validTeam = team.size() == 2 && team.at(0) == QChar(0x00A7) &&
        ((team.at(1) >= QLatin1Char('0') && team.at(1) <= QLatin1Char('9')) ||
         (team.at(1) >= QLatin1Char('a') && team.at(1) <= QLatin1Char('f')));
    if (!nameExpression.match(playerName).hasMatch() || !validTeam ||
        stars < 0 || level < 0 || winStreak < 0 || wins < 0 ||
        finalKills < 0 || bedsBroken < 0 || !std::isfinite(fkdr) ||
        !std::isfinite(wlr) || !std::isfinite(bblr) || fkdr < 0.0 ||
        wlr < 0.0 || bblr < 0.0) {
        return;
    }
    writeAgentCommand(QByteArrayLiteral("STATS ") + encodeProtocolToken(playerName) + ' ' +
                      encodeProtocolToken(team) + ' ' + QByteArray::number(stars) + ' ' +
                      QByteArray::number(fkdr, 'g', 9) + ' ' +
                      QByteArray::number(wlr, 'g', 9) + ' ' +
                      QByteArray::number(bblr, 'g', 9) + ' ' +
                      QByteArray::number(wins) + ' ' +
                      QByteArray::number(finalKills) + ' ' +
                      QByteArray::number(bedsBroken) + ' ' +
                      QByteArray::number(winStreak) + ' ' +
                      QByteArray::number(level) + '\n');
}

void OverlayManager::sendBlacklistCommand(const QByteArray &command)
{
    if (!m_authenticated || command.isEmpty() || command.size() > 2048 ||
        command.count('\n') != 1 || !command.endsWith('\n')) return;
    static const std::array<QByteArray, 7U> allowed{{
        QByteArrayLiteral("BLACKLIST_RESET\n"),
        QByteArrayLiteral("BLACKLIST_SETTINGS "),
        QByteArrayLiteral("BLACKLIST_PRESET "),
        QByteArrayLiteral("BLACKLIST_ENTRY "),
        QByteArrayLiteral("BLACKLIST_REMOVE "),
        QByteArrayLiteral("BLACKLIST_WARNING "),
        QByteArrayLiteral("BLACKLIST_SYNC_END\n")}};
    const bool permitted = std::any_of(allowed.cbegin(), allowed.cend(),
        [&command](const QByteArray &prefix) { return command.startsWith(prefix); });
    if (permitted) writeAgentCommand(command);
}

void OverlayManager::publishPlayerStatsError(const QString &playerName,
                                             const QString &reason)
{
    if (!m_authenticated) return;
    static const QRegularExpression nameExpression(
        QStringLiteral("^[A-Za-z0-9_]{1,16}$"));
    const QString safeReason = reason.simplified().left(96);
    if (!nameExpression.match(playerName).hasMatch() || safeReason.isEmpty()) return;
    writeAgentCommand(QByteArrayLiteral("STATS_ERROR ") +
                      encodeProtocolToken(playerName) + ' ' +
                      encodeProtocolToken(safeReason) + '\n');
}

void OverlayManager::publishMediaState(bool available, bool playing,
                                       const QString &title, const QString &artist,
                                       const QString &source, const QString &coverPath,
                                       qint64 positionMs, qint64 durationMs)
{
    if(!m_authenticated) return;
    const QString safeTitle = boundedUtf8(title.simplified(), 190);
    const QString safeArtist = boundedUtf8(artist.simplified(), 158);
    const QString safeSource = boundedUtf8(source.simplified(), 158);
    const QString safeCoverPath = boundedUtf8(coverPath, 510);
    writeAgentCommand(QByteArrayLiteral("MEDIA_STATE ") +
        QByteArray::number(available ? 1 : 0) + ' ' +
        QByteArray::number(playing ? 1 : 0) + ' ' +
        encodeProtocolToken(safeTitle) + ' ' + encodeProtocolToken(safeArtist) + ' ' +
        encodeProtocolToken(safeSource) + ' ' + encodeProtocolToken(safeCoverPath) + ' ' +
        QByteArray::number(std::max<qint64>(0,positionMs)) + ' ' +
        QByteArray::number(std::max<qint64>(0,durationMs)) + '\n');
}

void OverlayManager::publishMediaSpectrum(const QByteArray &bands)
{
    if (!m_authenticated || bands.size() > 80) return;
    const QList<QByteArray> values = bands.split(',');
    if (values.size() != 10) return;
    for (const QByteArray &value : values) {
        bool converted = false;
        const int band = value.toInt(&converted);
        if (!converted || band < 0 || band > 1000) return;
    }
    writeAgentCommand(QByteArrayLiteral("MEDIA_SPECTRUM ") + bands + '\n');
}

void OverlayManager::sendMediaSettings()
{
    if(!m_authenticated) return;
    QSettings settings;
    settings.beginGroup(QStringLiteral("MediaOverlay"));
    const bool enabled=settings.value(QStringLiteral("enabled"),true).toBool();
    const int storedOpacity=settings.value(QStringLiteral("opacity"),58).toInt();
    const int opacity=std::clamp(storedOpacity==86?58:storedOpacity,20,100);
    const int previous=std::clamp(settings.value(QStringLiteral("previousHotkey"),0xB1).toInt(),0,254);
    const int toggle=std::clamp(settings.value(QStringLiteral("toggleHotkey"),0xB3).toInt(),0,254);
    const int next=std::clamp(settings.value(QStringLiteral("nextHotkey"),0xB0).toInt(),0,254);
    const uint storedColor=std::min(settings.value(QStringLiteral("color"),0x857F82U).toUInt(),0xFFFFFFU);
    const uint color=storedColor==0x111318U?0x857F82U:storedColor;
    const int x=std::clamp(settings.value(QStringLiteral("x"),-1).toInt(),-1,1000);
    const int y=std::clamp(settings.value(QStringLiteral("y"),-1).toInt(),-1,1000);
    const int storedSpectrum=settings.value(QStringLiteral("spectrumOpacity"),100).toInt();
    const int spectrum=std::clamp(storedSpectrum==40?100:storedSpectrum,0,100);
    constexpr std::array<int,4> legacyScales{{42,52,68,84}};
    int scalePercent=settings.value(QStringLiteral("scalePercent"),-1).toInt();
    if(scalePercent<0) {
        const int legacy=std::clamp(
            settings.value(QStringLiteral("sizeIndex"),1).toInt(),0,3);
        scalePercent=legacyScales[static_cast<std::size_t>(legacy)];
    }
    scalePercent=std::clamp(scalePercent,35,100);
    if(storedOpacity!=opacity) settings.setValue(QStringLiteral("opacity"),opacity);
    if(storedColor!=color) settings.setValue(QStringLiteral("color"),color);
    if(storedSpectrum!=spectrum)
        settings.setValue(QStringLiteral("spectrumOpacity"),spectrum);
    settings.endGroup();
    writeAgentCommand(QByteArrayLiteral("MEDIA_SETTINGS ") +
        QByteArray::number(enabled ? 1 : 0) + ' ' + QByteArray::number(opacity) + ' ' +
        QByteArray::number(previous) + ' ' + QByteArray::number(toggle) + ' ' +
        QByteArray::number(next) + ' ' + QByteArray::number(color) + ' ' +
        QByteArray::number(x) + ' ' + QByteArray::number(y) + ' ' +
        QByteArray::number(spectrum) + ' ' + QByteArray::number(scalePercent) + '\n');
}

