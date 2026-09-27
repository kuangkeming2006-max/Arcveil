#include "MappingEventModel.h"
#include <QJsonArray>
#include <QJsonDocument>
namespace {
QString valueText(const QJsonValue &value) {
    if (value.isString())
        return value.toString().left(8192);
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact))
            .left(8192);
    if (value.isBool())
        return value.toBool() ? "true" : "false";
    return {};
}
} // namespace
QHash<int, QByteArray> MappingEventModel::roleNames() const {
    return {{Kind, "kind"},           {Time, "timeText"},         {Symbol, "symbol"},
            {Mapping, "mappingText"}, {Confidence, "confidence"}, {Evidence, "evidence"},
            {Reason, "reason"},       {Detail, "detail"},         {Accepted, "accepted"}};
}
QVariant MappingEventModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    return m_rows[index.row()].value(QString::fromLatin1(roleNames().value(role)));
}
void MappingEventModel::append(const QJsonObject &event) {
    QVariantMap row;
    row["kind"] = event.value("event").toString().left(64);
    row["timeText"] = event.value("time").toString().mid(11, 8);
    row["symbol"] = event.value("symbol").toString().left(256);
    row["mappingText"] = valueText(event.value("mapping"));
    row["confidence"] = event.contains("confidence") ? event.value("confidence").toDouble() : -1.0;
    row["evidence"] = valueText(event.value("evidence"));
    row["reason"] = event.value("reason").toString().left(8192);
    row["accepted"] = event.value("accepted").toBool();
    QString detail = event.value("phase").toString();
    if (event.contains("path"))
        detail = event.value("path").toString();
    if (event.contains("fingerprint"))
        detail = event.value("fingerprint").toString();
    if (event.contains("valid"))
        detail = event.value("valid").toBool() ? "Validation passed" : "Validation failed";
    if (event.contains("hit"))
        detail = event.value("hit").toBool() ? "Verified fingerprint cache hit"
                                             : "No verified cache entry";
    if (event.value("event") == "rollback")
        detail = event.value("success").toBool() ? "Previous mapping selected for next injection"
                                                 : "No valid previous mapping";
    if (event.contains("reason"))
        detail = event.value("reason").toString();
    if (event.value("event") == "progress")
        detail = QString("%1 — %2 / %3")
                     .arg(event.value("phase").toString())
                     .arg(event.value("completed").toInt())
                     .arg(event.value("total").toInt());
    row["detail"] = detail.left(8192);
    if (m_rows.size() >= 2000) {
        beginRemoveRows({}, 0, 0);
        m_rows.removeFirst();
        endRemoveRows();
    }
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows.push_back(std::move(row));
    endInsertRows();
}
void MappingEventModel::clear() {
    beginResetModel();
    m_rows.clear();
    endResetModel();
}
