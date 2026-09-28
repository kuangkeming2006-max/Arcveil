#pragma once
#include <QJsonObject>
#include <QString>
namespace mapping_cache {
QJsonObject readObject(const QString &path, qint64 limit = 2 * 1024 * 1024);
void writeObject(const QString &path, const QJsonObject &value);
QString fileDigest(const QString &path, qint64 limit = -1);
struct Entry {
    QString revision, fingerprint, pack, snapshot, digest, contractDigest;
    bool valid() const { return !revision.isEmpty(); }
};
class Cache final {
  public:
    explicit Cache(QString root) : m_root(std::move(root)) {}
    Entry lookup(const QString &fingerprint, const QString &contractDigest) const;
    Entry reference(const QString &contractDigest) const;
    void candidate(const QString &run, const QString &fingerprint, const QString &status);
    Entry promote(const QString &pack, const QString &snapshot, const QString &fingerprint,
                  const QString &contractDigest, const QJsonObject &validation);
    bool rollback();

  private:
    Entry entry(const QString &revision, const QString &contractDigest) const;
    QJsonObject index() const;
    QString m_root;
};
} // namespace mapping_cache
