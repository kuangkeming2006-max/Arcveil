#include "MappingProgressController.h"
#include <QJsonArray>
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
    m_stopRequested = false;
    reset(pid);
    emit openRequested();
}
void MappingProgressController::reset(quint32 pid) {
    m_symbols.clear();
    m_reference = {{"referenceAvailable", false}};
    m_snapshot.clear();
    m_matchingEnabled = !m_stopRequested;
    m_successful = false;
    m_steps = {"running", "pending", "pending", "pending"};
    m_status = QStringLiteral("正在准备 PID %1 的 mapping 检查").arg(pid);
    emit changed();
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
    m_stopRequested = true;
    m_matchingEnabled = false;
    if (!m_successful)
        step(2, "degraded");
    m_status = QStringLiteral("已停止后续匹配 / 重试；当前注入验证继续，已完成结果保留");
    emit stopMatchingRequested();
    emit matchingStopped();
    emit changed();
}
void MappingProgressController::consume(const QJsonObject &e) {
    const auto kind = e.value("event").toString();
    if (kind == "session-start") {
        reset(quint32(e.value("pid").toDouble()));
    } else if (kind == "step") {
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
            m_snapshot = fingerprint;
            m_reference["targetFingerprint"] = fingerprint;
        }
    } else if (kind.startsWith("symbol-")) {
        m_symbols.consume(e);
    } else if (kind == "snapshot-watch") {
        m_matchingEnabled = e.value("state") != "paused";
        if (!m_matchingEnabled)
            m_status =
                QStringLiteral("已停止 snapshot watch / retry；当前 attach 状态与已完成结果保留");
    } else if (kind == "waiting-for-runtime-change") {
        m_matchingEnabled = !e.value("paused").toBool();
        m_status =
            m_matchingEnabled
                ? QStringLiteral("等待运行时变化 · %1 个 unresolved · 已完成项为 provisional")
                      .arg(e.value("unresolved").toArray().size())
                : QStringLiteral("匹配已暂停；可继续匹配，当前 attach 状态保留");
    } else if (kind == "failure" || kind == "cancelled") {
        m_successful = false;
        m_matchingEnabled = false;
        for (int i = 0; i < 4; ++i)
            if (m_steps[i] == "running")
                step(i, "failed");
        step(3, "failed");
        m_status = e.value("reason").toString(QStringLiteral("检查已取消"));
    } else if (kind == "complete" && e.value("scope") == "mapping") {
        if (m_symbols.requiredComplete()) {
            const auto source = e.value("source").toString();
            m_status = source == "verified-cache" ? QStringLiteral("反混淆成功 · 来自已验证 Cache")
                       : source == "automatic"
                           ? QStringLiteral("反混淆成功 · 自动解析及最终验证通过")
                           : QStringLiteral("反混淆成功 · mapping pack 运行时验证通过");
        } else {
            step(3, "degraded");
            m_status = QStringLiteral("验证完成，但没有完整的 symbol 展示数据");
        }
    }
    emit changed();
}
