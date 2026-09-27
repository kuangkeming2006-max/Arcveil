#pragma once
#include <QAbstractListModel>
#include <QJsonObject>
#include <QVector>
class MappingEventModel final : public QAbstractListModel {
  public:
    enum Roles {
        Kind = Qt::UserRole + 1,
        Time,
        Symbol,
        Mapping,
        Confidence,
        Evidence,
        Reason,
        Detail,
        Accepted
    };
    explicit MappingEventModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : m_rows.size();
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void append(const QJsonObject &event);
    void clear();

  private:
    QVector<QVariantMap> m_rows;
};
