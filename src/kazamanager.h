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
    QTcpServer m_remotecontrol;
    QList<KaZaRemoteConnection*> m_remoteclients;
    QString m_appFilename;

    static KaZaManager *m_instance;

public:
    explicit KaZaManager(QObject *parent = nullptr);

    static KaZaManager *getInstance();
    static void registerObject(KaZaObject* obj);
    static KaZaObject* getObject(const QString &name);
    static QVariant setting(QString id);
    static QString appChecksum();
    static QString appFilename();


private slots:
    void _pendingConnectionAvailable();
    void _disconnection();
    void _pendingRemoteConnectionAvailable();
    void _remoteDisconnection();

};

#endif // KAZAMANAGER_H
