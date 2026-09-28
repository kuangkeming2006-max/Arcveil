#pragma once
#include <QAbstractListModel>
#include <QJsonObject>
#include <QSortFilterProxyModel>

// Presentation state only. Never writes packs, starts a JVM job or touches the Agent.
class MappingProgressModel final : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Role {
        Symbol = Qt::UserRole + 1,
        LogicalName,
        RuntimeName,
        Status,
        Confidence,
        Evidence,
        Reason,
        Settling,
        Required,
        Attempts
    };
    struct Row {
        QString symbol, logicalName, runtimeName, status = "pending", evidence, reason;
        double confidence = 0;
        bool required = false, settling = false, matched = false;
        int attempts = 0;
    };
    explicit MappingProgressModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void clear();
    void consume(const QJsonObject &event);
    void retryUnfinished();
    int count(const QString &status) const;
    bool requiredComplete() const;
    bool contains(const QString &symbol) const;
  signals:
    void changed();

  private:
    int find(const QString &symbol) const;
    QList<Row> m_rows;
    quint64 m_generation = 0;
};
class MappingProgressFilter final : public QSortFilterProxyModel {
    Q_OBJECT
  public:
    MappingProgressFilter(MappingProgressModel *source, QString status, QObject *parent);

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

  private:
    QString m_status;
};
