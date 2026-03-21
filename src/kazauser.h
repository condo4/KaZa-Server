#ifndef KAZAUSER_H
#define KAZAUSER_H

#include <QObject>
#include <QList>

class KaZaConnection;
class KaZaObject;

class KaZaUser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

    QString m_name;
    QList<KaZaConnection*> m_connections;
    KaZaObject *m_connectedObject;

public:
    explicit KaZaUser(QObject *parent = nullptr);
    explicit KaZaUser(const QString &name, QObject *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name);

    bool connected() const { return !m_connections.isEmpty(); }

    void addConnection(KaZaConnection *connection);
    void removeConnection(KaZaConnection *connection);

    const QList<KaZaConnection*>& connections() const { return m_connections; }

signals:
    void nameChanged();
    void connectedChanged(bool connected);
};

#endif // KAZAUSER_H
