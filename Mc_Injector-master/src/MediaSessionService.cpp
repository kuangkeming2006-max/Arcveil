#include "MediaSessionService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>

MediaSessionService::MediaSessionService(QObject *parent) : QObject(parent)
{
    connect(&m_bridge, &QProcess::readyReadStandardOutput,
            this, &MediaSessionService::readOutput);
    connect(&m_bridge, &QProcess::readyReadStandardError, this, [this] {
        const QString diagnostic = QString::fromLocal8Bit(
            m_bridge.readAllStandardError()).trimmed();
        if (!diagnostic.isEmpty())
            qWarning().noquote() << "Windows media bridge:" << diagnostic;
    });
    connect(&m_bridge, &QProcess::errorOccurred, this, [this] {
        clearState();
        scheduleRestart();
    });
    connect(&m_bridge, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        if (!m_shuttingDown)
            qWarning() << "Windows media bridge exited" << exitCode << status;
        clearState();
        scheduleRestart();
    });
    QTimer::singleShot(0, this, &MediaSessionService::start);
}

MediaSessionService::~MediaSessionService()
{
    m_shuttingDown = true;
    send(QByteArrayLiteral("QUIT"));
    m_bridge.closeWriteChannel();
    if (!m_bridge.waitForFinished(800)) m_bridge.kill();
}

void MediaSessionService::start()
{
    m_restartScheduled = false;
    if (m_shuttingDown || m_bridge.state() != QProcess::NotRunning) return;
    const QString executable = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("tools/WindowsMediaBridge.exe"));
    if (!QFileInfo::exists(executable)) return;
    m_bridge.setProgram(executable);
    m_bridge.setProcessChannelMode(QProcess::SeparateChannels);
    m_bridge.start();
}

void MediaSessionService::scheduleRestart()
{
    if (m_shuttingDown || m_restartScheduled) return;
    m_restartScheduled = true;
    QTimer::singleShot(1500, this, &MediaSessionService::start);
}

QString MediaSessionService::decode(const QByteArray &value)
{
    return QString::fromUtf8(QByteArray::fromBase64(value));
}

void MediaSessionService::readOutput()
{
    m_buffer += m_bridge.readAllStandardOutput();
    for (;;) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        const QList<QByteArray> fields = line.split('\t');
        if (fields.isEmpty()) continue;
        if (fields[0] == QByteArrayLiteral("SPECTRUM") && fields.size() == 2) {
            const QList<QByteArray> values = fields[1].split(',');
            bool valid = values.size() == 10;
            for (const QByteArray &value : values) {
                bool converted = false;
                const int band = value.toInt(&converted);
                valid = valid && converted && band >= 0 && band <= 1000;
            }
            if (valid) {
                m_spectrum = fields[1];
                emit spectrumChanged(m_spectrum);
            }
            continue;
        }
        if (fields[0] == QByteArrayLiteral("ERROR")) {
            const QString message = fields.size() >= 2 ? decode(fields[1])
                                                       : QStringLiteral("unknown error");
            qWarning().noquote() << "Windows media bridge reported:" << message;
            continue;
        }
        if (fields[0] == QByteArrayLiteral("ACTION") && fields.size() >= 4) {
            const bool accepted = fields[2] == QByteArrayLiteral("1");
            const QString detail = fields.size() >= 5 ? decode(fields[4]) : QString{};
            if (accepted)
                qInfo().noquote() << "Now Playing action" << fields[1]
                                  << "accepted via" << fields[3];
            else
                qWarning().noquote() << "Now Playing action" << fields[1]
                                     << "failed via" << fields[3] << detail;
            continue;
        }
        if (fields[0] == QByteArrayLiteral("EMPTY")) {
            m_available = false; m_playing = false; m_title.clear();
            m_artist.clear(); m_source.clear(); m_positionMs = m_durationMs = 0;
            emit stateChanged(false, false, {}, {}, {}, {}, 0, 0);
            m_spectrum = QByteArrayLiteral("0,0,0,0,0,0,0,0,0,0");
            emit spectrumChanged(m_spectrum);
            continue;
        }
        if (fields[0] != QByteArrayLiteral("STATE") || fields.size() < 8) continue;
        m_available = true;
        m_playing = fields[1] == QByteArrayLiteral("1");
        m_title = decode(fields[2]); m_artist = decode(fields[3]); m_source = decode(fields[4]);
        m_positionMs = fields[5].toLongLong(); m_durationMs = fields[6].toLongLong();
        const QByteArray cover = QByteArray::fromBase64(fields[7]);
        const bool coverChanged = fields.size() >= 9 &&
            fields[8] == QByteArrayLiteral("1");
        if (coverChanged) m_coverPath.clear();
        if (!cover.isEmpty()) {
            const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            QDir().mkpath(directory);
            const QString path = QDir(directory).filePath(QStringLiteral("now-playing-cover.bin"));
            QSaveFile output(path);
            if (output.open(QIODevice::WriteOnly) && output.write(cover) == cover.size() && output.commit())
                m_coverPath = QDir::toNativeSeparators(path);
        }
        emit stateChanged(m_available, m_playing, m_title, m_artist, m_source,
                          m_coverPath, m_positionMs, m_durationMs);
    }
}

void MediaSessionService::send(const QByteArray &command)
{
    if (m_bridge.state() == QProcess::Running) {
        qInfo().noquote() << "Now Playing action queued:" << command;
        m_bridge.write(command + '\n');
    } else {
        qWarning().noquote() << "Now Playing action rejected; bridge is not running:"
                             << command;
    }
}

void MediaSessionService::clearState()
{
    m_available = false; m_playing = false; m_title.clear();
    m_artist.clear(); m_source.clear(); m_coverPath.clear();
    m_positionMs = m_durationMs = 0;
    emit stateChanged(false, false, {}, {}, {}, {}, 0, 0);
    m_spectrum = QByteArrayLiteral("0,0,0,0,0,0,0,0,0,0");
    emit spectrumChanged(m_spectrum);
}

void MediaSessionService::previous() { send(QByteArrayLiteral("PREVIOUS")); }
void MediaSessionService::next() { send(QByteArrayLiteral("NEXT")); }
void MediaSessionService::togglePlayback() { send(QByteArrayLiteral("TOGGLE")); }

void MediaSessionService::republish()
{
    emit stateChanged(m_available, m_playing, m_title, m_artist, m_source,
                      m_coverPath, m_positionMs, m_durationMs);
    emit spectrumChanged(m_spectrum);
}
