#include "MappingProgressModel.h"
#include <QJsonArray>
#include <QTimer>
int MappingProgressModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}
QVariant MappingProgressModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto &r = m_rows[index.row()];
    switch (role) {
    case Symbol:
        return r.symbol;
    case LogicalName:
        return r.logicalName;
    case RuntimeName:
        return r.runtimeName;
    case Status:
        return r.status;
    case Confidence:
        return r.confidence;
    case Evidence:
        return r.evidence;
    case Reason:
        return r.reason;
    case Settling:
        return r.settling;
    case Required:
        return r.required;
    case Attempts:
        return r.attempts;
    default:
        return {};
    }
}
QHash<int, QByteArray> MappingProgressModel::roleNames() const {
    return {{Symbol, "symbol"},       {LogicalName, "logicalName"}, {RuntimeName, "runtimeName"},
            {Status, "symbolStatus"}, {Confidence, "confidence"},   {Evidence, "evidence"},
            {Reason, "reason"},       {Settling, "settling"},       {Required, "isRequired"},
            {Attempts, "attempts"}};
}
int MappingProgressModel::find(const QString &key) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].symbol == key)
            return i;
    return -1;
}
bool MappingProgressModel::contains(const QString &key) const { return find(key) >= 0; }
void MappingProgressModel::clear() {
    ++m_generation;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit changed();
}
int MappingProgressModel::count(const QString &status) const {
    int n = 0;
    for (const auto &r : m_rows)
        n += r.status == status;
    return n;
}
bool MappingProgressModel::requiredComplete() const {
    bool any = false;
    for (const auto &r : m_rows)
        if (r.required) {
            any = true;
            if (!r.matched)
                return false;
        }
    return any;
}
void MappingProgressModel::retryUnfinished() {
    for (int i = 0; i < m_rows.size(); ++i) {
        auto &r = m_rows[i];
        if (r.matched)
            continue;
        r.status = "pending";
        r.reason.clear();
        ++r.attempts;
        emit dataChanged(index(i), index(i));
    }
    emit changed();
}
void MappingProgressModel::consume(const QJsonObject &e) {
    const auto key = e.value("symbol").toString();
    if (key.isEmpty())
        return;
    const auto event = e.value("event").toString();
    int i = find(key);
    if (i < 0) {
        if (event != "symbol-queued")
            return; // Only schema creates rows; ignores unrelated/unbounded keys.
        i = m_rows.size();
        beginInsertRows({}, i, i);
        Row r;
        r.symbol = key;
        r.logicalName = e.value("logicalName").toString(key);
        r.required = e.value("required").toBool();
        m_rows.append(r);
        endInsertRows();
    }
    auto &r = m_rows[i];
    if (r.matched)
        return; // Accepted results remain stable, including during migration.
    if (event == "symbol-started" || event == "symbol-progress")
        r.status = "active";
    else if (event == "symbol-retry") {
        r.status = "pending";
        ++r.attempts;
    } else if (event == "symbol-failed")
        r.status = "pending";
    else if (event == "symbol-matched") {
        const auto runtime = e.value("runtimeName").toString();
        if (runtime.isEmpty() || !e.value("verified").toBool())
            return;
        r.runtimeName = runtime;
        r.matched = true;
        r.settling = true;
        r.status = "active";
        const auto generation = m_generation;
        QTimer::singleShot(750, this, [this, key, generation] {
            if (generation != m_generation)
                return;
            const int row = find(key);
            if (row < 0)
                return;
            m_rows[row].status = "completed";
            m_rows[row].settling = false;
            emit dataChanged(index(row), index(row));
            emit changed();
        });
    }
    if (e.contains("runtimeName") && !r.matched)
        r.runtimeName = e.value("runtimeName").toString();
    if (e.contains("confidence"))
        r.confidence = e.value("confidence").toDouble();
    if (e.contains("evidence")) {
        QStringList list;
        for (const auto &v : e.value("evidence").toArray())
            list << v.toString();
        r.evidence = list.join(" · ");
    }
    r.reason = e.value("reason").toString();
    emit dataChanged(index(i), index(i));
    emit changed();
}
MappingProgressFilter::MappingProgressFilter(MappingProgressModel *source, QString status,
                                             QObject *parent)
    : QSortFilterProxyModel(parent), m_status(std::move(status)) {
    setSourceModel(source);
    setDynamicSortFilter(true);
}
bool MappingProgressFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    return sourceModel()->data(sourceModel()->index(row, 0, parent),
                               MappingProgressModel::Status) == m_status;
}
