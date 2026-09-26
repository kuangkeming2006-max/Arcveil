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
constexpr qint64 kGameStateStaleAfterMilliseconds = 3500;
}

void OverlayManager::resetGameState()
{
    const bool changed = m_gameStateReceived || m_gameStateAvailable
                      || m_gameStateStale || m_playerHealth != 0.0
                      || m_playerMaxHealth != 0.0 || m_playerEntityId != 0
                      || m_playerX != 0.0 || m_playerY != 0.0
                      || m_playerZ != 0.0 || m_loadedEntities != 0
                      || m_bedCount != 0 || !m_mappingProfile.isEmpty()
                      || m_mappingState != QStringLiteral("waiting")
                      || m_gameStateTimestamp != 0 || m_gameStateSequence != 0;
    m_gameStateReceived = false;
    m_gameStateAvailable = false;
    m_gameStateStale = false;
    m_playerHealth = 0.0;
    m_playerMaxHealth = 0.0;
    m_playerEntityId = 0;
    m_playerX = 0.0;
    m_playerY = 0.0;
    m_playerZ = 0.0;
    m_loadedEntities = 0;
    m_bedCount = 0;
    m_mappingProfile.clear();
    m_mappingState = QStringLiteral("waiting");
    m_gameStateTimestamp = 0;
    m_lastGameStateReceiptElapsedMs = 0;
    m_gameStateSequence = 0;
    if (!m_playerName.isEmpty()) {
        m_playerName.clear();
        emit playerStatusChanged();
    }
    if (m_matchActive) {
        m_matchActive = false;
        emit matchStateChanged(false);
    }
    if (changed)
        emit gameStateChanged();
}

void OverlayManager::refreshGameStateFreshness()
{
    if (!m_gameStateReceived || m_gameStateStale)
        return;
    if (m_gameStateReceiptClock.elapsed() - m_lastGameStateReceiptElapsedMs
        <= kGameStateStaleAfterMilliseconds) {
        return;
    }
    m_gameStateStale = true;
    emit gameStateChanged();
}

