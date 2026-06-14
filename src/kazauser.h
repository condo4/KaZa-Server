#ifndef KAZAUSER_H
#define KAZAUSER_H

#include <QObject>
#include <QList>
#include <QVariant>

class KaZaConnection;
class KaZaObject;

class KaZaUser : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KaZaUser)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(double latitude READ latitude NOTIFY positionChanged)
    Q_PROPERTY(double longitude READ longitude NOTIFY positionChanged)
    Q_PROPERTY(double altitude READ altitude NOTIFY positionChanged)
    Q_PROPERTY(double accuracy READ accuracy NOTIFY positionChanged)

    QString m_name;
    QList<KaZaConnection*> m_connections;
    KaZaObject *m_connectedObject;
    KaZaObject *m_latitudeObject;
    KaZaObject *m_longitudeObject;
    KaZaObject *m_altitudeObject;
    KaZaObject *m_accuracyObject;
    double m_latitude {0.0};
    double m_longitude {0.0};
    double m_altitude {0.0};
    double m_accuracy {0.0};

public:
    explicit KaZaUser(QObject *parent = nullptr);
    explicit KaZaUser(const QString &name, QObject *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name);

    bool connected() const { return !m_connections.isEmpty(); }

    double latitude() const { return m_latitude; }
    double longitude() const { return m_longitude; }
    double altitude() const { return m_altitude; }
    double accuracy() const { return m_accuracy; }

    void setPosition(double latitude, double longitude, double altitude, double accuracy);

    void addConnection(KaZaConnection *connection);
    void removeConnection(KaZaConnection *connection);

private:
    void ensureObject(KaZaObject *&ptr, const QString &fullName, const QVariant &initialValue);

    const QList<KaZaConnection*>& connections() const { return m_connections; }

signals:
    void nameChanged();
    void connectedChanged(bool connected);
    void positionChanged(double latitude, double longitude, double altitude, double accuracy);
};

#endif // KAZAUSER_H
