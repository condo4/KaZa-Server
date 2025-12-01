#ifndef KAZAMANAGER_H
#define KAZAMANAGER_H

#include <QObject>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QSslServer>

// #define DEBUG_KNX
// #define DEBUG_CONNECTION

class KaZaObject;
class KaZaConnection;
class KzAlarm;
class KaZaRemoteConnection;
class QSqlDatabase;


class KaZaManager : public QObject
{
    Q_OBJECT
    QSettings m_settings;
    QQmlApplicationEngine engine;
    QList<KaZaObject*> m_objects;
    QSslServer m_server;
    QList<KaZaConnection*> m_clients;
    QList<KzAlarm*> m_alarms;
    QSslServer m_remotecontrol;
    QList<KaZaRemoteConnection*> m_remoteclients;
    QString m_appFilename;
    bool m_databaseReady {false};
    bool m_initialized {false};

    static KaZaManager *m_instance;

public:
    explicit KaZaManager(QObject *parent = nullptr);

    bool isInitialized() const { return m_initialized; }
    static KaZaManager *getInstance();
    static void registerObject(KaZaObject* obj);
    static void registerAlarm(KzAlarm* obj);
    static const QList<KzAlarm*>& alarms();
    static KaZaObject* getObject(const QString &name);
    static QStringList getObjectKeys();
    static QVariant setting(QString id);
    static QString appChecksum();
    static QString appFilename();
    static void sendNotify(QString text);
    static void askPosition();
    static void sendObjectsList();

public slots:
    bool runDbQuery(const QString &query) const;

private:
    bool ensureCertificatesExist();

private slots:
    void _pendingConnectionAvailable();
    void _disconnection();
    void _pendingRemoteConnectionAvailable();
    void _remoteDisconnection();

signals:
    void objectAdded();
    void alarmAdded();

};

#endif // KAZAMANAGER_H
