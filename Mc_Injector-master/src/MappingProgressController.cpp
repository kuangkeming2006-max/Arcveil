#include "MappingProgressController.h"
MappingProgressController::MappingProgressController(QObject *parent)
    : QObject(parent), m_active(&m_symbols, "active", this),
      m_completed(&m_symbols, "completed", this), m_pending(&m_symbols, "pending", this) {
    connect(&m_symbols, &MappingProgressModel::changed, this, [this] {
        if (m_symbols.requiredComplete() && !m_successful) {
            m_successful = true;
            m_matchingEnabled = false;
            step(2, "success");
            step(3, "success");
            m_status = QStringLiteral("反混淆成功 · required symbols 已验证");
            emit matchingStopped();
        }
        emit changed();
    });
    m_steps = {"pending", "pending", "pending", "pending"};
}
void MappingProgressController::begin(quint32 pid) {
    m_symbols.clear();
    m_reference = {{"referenceAvailable", false}};
    m_snapshot.clear();
    m_matchingEnabled = true;
    m_successful = false;
    m_steps = {"running", "pending", "pending", "pending"};
    m_status = QStringLiteral("正在准备 PID %1 的 mapping 检查").arg(pid);
    emit changed();
    emit openRequested();
}
void MappingProgressController::step(int i, const QString &state) {
    if (i < 0 || i >= 4 ||
        !QStringList{"pending", "running", "success", "failed", "degraded"}.contains(state))
        return;
    m_steps[i] = state;
}
void MappingProgressController::stopMatching() {
    if (!m_matchingEnabled)
        return;
    m_matchingEnabled = false;
    if (!m_successful)
        step(2, "degraded");
    m_status = QStringLiteral("已停止后续匹配 / 重试；当前注入验证继续，已完成结果保留");
    emit matchingStopped();
    emit changed();
}
void MappingProgressController::consume(const QJsonObject &e) {
    const auto kind = e.value("event").toString();
    if (kind == "step") {
        step(e.value("index").toInt(-1), e.value("state").toString());
        if (e.contains("message"))
            m_status = e.value("message").toString();
    } else if (kind == "reference") {
        for (auto it = e.begin(); it != e.end(); ++it)
            if (it.key() != "event")
                m_reference[it.key()] = it.value().toVariant();
    } else if (kind == "snapshot-update") {
        const auto fingerprint = e.value("fingerprint").toString();
        if (!fingerprint.isEmpty() && fingerprint != m_snapshot) {
            if (!m_snapshot.isEmpty() && m_matchingEnabled)
                m_symbols.retryUnfinished();
            m_snapshot = fingerprint;
            m_reference["targetFingerprint"] = fingerprint;
        }
    } else if (kind.startsWith("symbol-")) {
        if (kind != "symbol-retry" || m_matchingEnabled)
            m_symbols.consume(e);
    } else if (kind == "failure" || kind == "cancelled") {
        m_successful = false;
        m_matchingEnabled = false;
        for (int i = 0; i < 4; ++i)
            if (m_steps[i] == "running")
                step(i, "failed");
        step(3, "failed");
        m_status = e.value("reason").toString(QStringLiteral("检查已取消"));
    } else if (kind == "complete" && e.value("scope") == "mapping") {
        if (!m_symbols.requiredComplete()) {
            step(3, "degraded");
            m_status = QStringLiteral("验证完成，但没有完整的 symbol 展示数据");
        }
    }
    emit changed();
}
