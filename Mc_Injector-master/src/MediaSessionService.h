#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QByteArray>

class MediaSessionService final : public QObject
{
    Q_OBJECT
public:
    explicit MediaSessionService(QObject *parent = nullptr);
    ~MediaSessionService() override;

signals:
    void stateChanged(bool available, bool playing, const QString &title,
                      const QString &artist, const QString &source,
                      const QString &coverPath, qint64 positionMs,
                      qint64 durationMs);
    // Ten comma-separated 0..1000 bands from WASAPI loopback.
    void spectrumChanged(const QByteArray &bands);

public slots:
    void previous();
    void next();
    void togglePlayback();
    // Media metadata can arrive before the Agent has authenticated. Re-emit
    // the complete cached snapshot when a new IPC session becomes ready.
    void republish();

private:
    void start();
    void scheduleRestart();
    void readOutput();
    void send(const QByteArray &command);
    void clearState();
    static QString decode(const QByteArray &value);

    QProcess m_bridge;
    QByteArray m_buffer;
    QString m_coverPath;
    bool m_available = false;
    bool m_playing = false;
    QString m_title;
    QString m_artist;
    QString m_source;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    QByteArray m_spectrum = QByteArrayLiteral("0,0,0,0,0,0,0,0,0,0");
    bool m_shuttingDown = false;
    bool m_restartScheduled = false;
};
