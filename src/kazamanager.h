#ifndef KAZAMANAGER_H
#define KAZAMANAGER_H

#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QSslServer>
#include <QSqlDatabase>
#include <QHash>
#include <QJSValue>
#include "kazaservicemanager.h"

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
    KaZaServiceManager m_services;

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
    static QPointer<KaZaUser> findUser(const QString &username);

public slots:
    bool runDbQuery(const QString &query);
    // Read-only query (see KaZaServiceManager::runQuery). `callback(result)` is called later with
    // { ok, status, errorCode, columns, rows, truncated }, rows being arrays of values.
    void query(const QString &sql, const QVariantMap &bindings, const QJSValue &callback);
    void notify(QString message);
    void logTemperature(const QVariant &rawid, const QString &name, const QVariant &value);
    void logMeasurement(const QString &domain, const QVariant &rawid, const QString &name, const QVariant &value);

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
