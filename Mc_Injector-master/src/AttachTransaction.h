#pragma once
#include <QString>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QUuid>
#include <memory>

// Owned by OverlayManager; selection is never a session capability.
struct AttachTransaction {
    enum class State { Idle, Selected, MappingFastPath, MappingResolving, MappingPaused,
                       MappingVerified, LoadingAgent, Active, Cancelling, Detached, Failed };
    QString transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    quint32 pid = 0;
    QString processStart;
    State state = State::Idle;
    bool valid = true;
    quint64 generation = 0;
    QElapsedTimer elapsed, loaderElapsed, agentElapsed;
    QJsonObject stageMs;
    int analyzerProcesses = 0, helperProcesses = 0, detailCaptureCalls = 0, autoResolveCalls = 0;
    qint64 jvmtiCalls = 0, capturedBytes = 0, bytecodeBytes = 0, constantPoolBytes = 0;
    bool cacheHit = false;
    qint64 jniCalls = 0;
    void invalidate() { valid = false; ++generation; }
    QString stateName() const {
        switch (state) {
#define ATTACH_STATE(s) case State::s: return QStringLiteral(#s);
        ATTACH_STATE(Idle) ATTACH_STATE(Selected) ATTACH_STATE(MappingFastPath)
        ATTACH_STATE(MappingResolving) ATTACH_STATE(MappingPaused) ATTACH_STATE(MappingVerified)
        ATTACH_STATE(LoadingAgent) ATTACH_STATE(Active) ATTACH_STATE(Cancelling)
        ATTACH_STATE(Detached) ATTACH_STATE(Failed)
#undef ATTACH_STATE
        }
        return {};
    }
};
