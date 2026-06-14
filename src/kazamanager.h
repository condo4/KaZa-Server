#ifndef KAZAMANAGER_H
#define KAZAMANAGER_H

#include <QObject>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QSslServer>
#include <QSqlDatabase>
#include <QHash>

// #define DEBUG_KNX
// #define DEBUG_CONNECTION

class KaZaObject;
class KaZaConnection;
class KzAlarm;
class KaZaRemoteConnection;
class KaZaUser;


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
    QSqlDatabase m_database;
    QHash<QString, KaZaUser*> m_users;

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
    static const QList<KaZaObject*>& objects();
    static QVariant setting(QString id);
    static QString appChecksum();
    static QString appFilename();
    static void sendNotify(QString text);
    static void askPosition(QString param);
    static void sendObjectsList();
    static KaZaUser* findUser(const QString &username);

public slots:
    bool runDbQuery(const QString &query);
    void notify(QString message);

private:
    bool ensureCertificatesExist();

private slots:
    void _pendingConnectionAvailable();
    void _userAuthenticated();
    void _disconnection();
    void _pendingRemoteConnectionAvailable();
    void _remoteDisconnection();

signals:
    void objectAdded();
    void alarmAdded();

};

#endif // KAZAMANAGER_H
