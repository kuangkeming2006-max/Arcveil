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

namespace {
constexpr qsizetype kMaximumAgentMessageBytes = 64 * 1024;
constexpr qint64 kAgentReadChunkBytes = 4096;
}

void OverlayManager::acceptAgentConnection()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *candidate = m_server.nextPendingConnection();
        if (!candidate)
            continue;

        if (m_agentSocket) {
            candidate->disconnectFromServer();
            candidate->deleteLater();
            continue;
        }

        m_agentSocket = candidate;
        candidate->setReadBufferSize(kMaximumAgentMessageBytes + 1);
        m_agentReadBuffer.clear();
        connect(candidate, &QLocalSocket::readyRead,
                this, &OverlayManager::readAgentMessages);
        connect(candidate, &QLocalSocket::disconnected,
                this, &OverlayManager::handleAgentDisconnected);
        readAgentMessages();
    }
}

void OverlayManager::readAgentMessages()
{
    if (!m_agentSocket)
        return;

    QLocalSocket *const source = m_agentSocket;
    QElapsedTimer budget;
    budget.start();
    int processedLines = 0;
    qint64 readBytes = 0;
    // Telemetry bursts must yield to paint/input events. In particular, drain
    // buffered complete lines on the next turn even if readyRead never fires
    // again (all bytes may already have reached the user-space buffer).
    while (m_agentSocket == source) {
        if (processedLines >= 64 || readBytes >= 64 * 1024 || budget.elapsed() >= 3)
            break;
        const qsizetype newline = m_agentReadBuffer.indexOf('\n');
        if (newline >= 0) {
            // Apply the limit to each wire line, not to a readAll() batch that
            // may legitimately contain many small telemetry messages.
            if (newline > kMaximumAgentMessageBytes) {
                fail(QStringLiteral("IPC_MESSAGE_TOO_LARGE"),
                     QStringLiteral("The native agent exceeded the per-line IPC message limit."));
                return;
            }
            const QByteArray line = m_agentReadBuffer.left(newline).trimmed();
            m_agentReadBuffer.remove(0, newline + 1);
            if (!line.isEmpty())
                processAgentLine(line);
            ++processedLines;
            if (m_agentSocket != source)
                return;
            continue;
        }

        // No newline is buffered, so these bytes all belong to one partial
        // line. Bound it independently of how the named-pipe read was split.
        if (m_agentReadBuffer.size() > kMaximumAgentMessageBytes) {
            fail(QStringLiteral("IPC_MESSAGE_TOO_LARGE"),
                 QStringLiteral("The native agent exceeded the per-line IPC message limit."));
            return;
        }
        if (source->bytesAvailable() <= 0) break;
        const QByteArray chunk = source->read(kAgentReadChunkBytes);
        if (chunk.isEmpty()) break;
        readBytes += chunk.size();
        m_agentReadBuffer += chunk;
    }
    if (m_agentSocket == source && !m_agentReadBuffer.contains('\n') &&
        m_agentReadBuffer.size() > kMaximumAgentMessageBytes) {
        fail(QStringLiteral("IPC_MESSAGE_TOO_LARGE"),
             QStringLiteral("The native agent exceeded the per-line IPC message limit."));
        return;
    }
    if (m_agentSocket == source &&
        (source->bytesAvailable() > 0 || m_agentReadBuffer.contains('\n')) &&
        !m_agentReadScheduled) {
        m_agentReadScheduled = true;
        QTimer::singleShot(1, this, [this] {
            m_agentReadScheduled = false;
            readAgentMessages();
        });
    }
}

void OverlayManager::writeAgentCommand(const QByteArray &command)
{
    if (!m_agentSocket || m_agentSocket->state() != QLocalSocket::ConnectedState)
        return;
    m_agentSocket->write(command);
}

