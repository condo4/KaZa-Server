#include "kazamanager.h"
#include "kazaconnection.h"
#include "kazaremoteconnection.h"
#include "kazaobject.h"
#include "kazaelement.h"
#include "kzobject.h"
#include "scheduler.h"
#include "kzalarm.h"

#include <QUrl>
#include <QQmlContext>
#include <QFile>
#include <QSslKey>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

KaZaManager *KaZaManager::m_instance = {nullptr};

KaZaManager::KaZaManager(QObject *parent)
    : QObject{parent}
    , m_settings("/etc/KaZaServer.conf", QSettings::Format::IniFormat)
{
    QString qmlconf = m_settings.value("Server/qml").toString();
    m_instance = this;

    qmlRegisterType<KaZaObject>("org.kazoe.kaza", 1, 0, "KaZaObject");
    qmlRegisterType<KaZaElement>("org.kazoe.kaza", 1, 0, "KaZaElement");
    qmlRegisterType<KzObject>("org.kazoe.kaza", 1, 0, "KzObject");
    qmlRegisterType<KzAlarm>("org.kazoe.kaza", 1, 0, "KzAlarm");
    qmlRegisterType<Scheduler>("org.kazoe.kaza", 1, 0, "Scheduler");
    if(!qmlconf.isEmpty())
    {
        qDebug() << "Start QML config" << qmlconf;
        const QUrl url(qmlconf);
        engine.rootContext()->setContextProperty("kazamanager", this);
        engine.load(url);
    }
    else
    {
        qInfo() << "No QML module loaded";
    }

    QString cafile = m_settings.value("ssl/ca_file").toString();
    QString certfile = m_settings.value("ssl/cert_file").toString();
    QSslConfiguration configuration;
    configuration.setCaCertificates(QSslCertificate::fromPath(cafile));
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    QList<QSslCertificate> certfilelist = QSslCertificate::fromPath(certfile);
    configuration.setLocalCertificate(certfilelist.first());

    QFile key(m_settings.value("ssl/private_key_file").toString());
    if(!key.open(QFile::ReadOnly)) {
        qWarning() << "Couldn't open private key:" << key.errorString();
        return;
    }
    QSslKey sslkey(&key, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, m_settings.value("ssl/private_key_password").toByteArray());
    configuration.setPrivateKey(sslkey);
    m_server.setSslConfiguration(configuration);

    QObject::connect(&m_server, &QSslServer::pendingConnectionAvailable, this, &KaZaManager::_pendingConnectionAvailable);
    QObject::connect(&m_server, &QSslServer::errorOccurred, [this](QSslSocket *socket, QAbstractSocket::SocketError socketError){
        qWarning() << "SSL ERROR" << socketError;
    });
    QObject::connect(&m_server, &QSslServer::peerVerifyError, [this](QSslSocket *socket, const QSslError &error){
        qDebug() << "SSL peerVerifyError " << error;
    });

    QObject::connect(&m_server, &QSslServer::errorOccurred, [this](QSslSocket *socket, QAbstractSocket::SocketError socketError){
        qWarning() << "SSL ERROR" << socketError;
    });

    m_server.listen(QHostAddress::Any, m_settings.value("ssl/port").toInt());
    qInfo() << "Start SSL server, listen on"  << m_server.serverPort();

    QObject::connect(&m_remotecontrol, &QTcpServer::pendingConnectionAvailable, this, &KaZaManager::_pendingRemoteConnectionAvailable);
    m_remotecontrol.listen(QHostAddress::Any, m_settings.value("scpi/port").toInt());
    qInfo() << "Start Remote Control, listen on"  << m_remotecontrol.serverPort();

    /* Calculate current App Checksum */
    m_appFilename = m_settings.value("Client/app").toString();

    QString dbdriver = m_settings.value("database/driver").toString();
    if(!dbdriver.isEmpty())
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(m_settings.value("database/driver").toString());
        database.setDatabaseName(m_settings.value("database/dbName").toString());
        database.setHostName(m_settings.value("database/hostname").toString());
        database.setPort(m_settings.value("database/port").toInt());
        if(!database.open(m_settings.value("database/username").toString(), m_settings.value("database/password").toString()))
        {
            qWarning() << "Database open error: " << database.lastError();
        }
        else
        {
            m_databaseReady = true;
        }
    }
}

KaZaManager *KaZaManager::getInstance() {
    return m_instance;
}

void KaZaManager::registerObject(KaZaObject* obj) {
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
    }
    m_instance->m_objects.append(obj);
    emit m_instance->objectAdded();
}

void KaZaManager::registerAlarm(KzAlarm *obj)
{
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
    }
    m_instance->m_alarms.append(obj);
    emit m_instance->alarmAdded();
}

const QList<KzAlarm *> &KaZaManager::alarms()
{
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
    }
    return m_instance->m_alarms;
}

KaZaObject* KaZaManager::getObject(const QString &name) {
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
    }
    for(KaZaObject* obj: std::as_const(m_instance->m_objects))
    {
        if(obj->name() == name)
        {
            return obj;
        }
    }
    return nullptr;
}

QStringList KaZaManager::getObjectKeys()
{
    QStringList res;
    for(KaZaObject* obj: std::as_const(m_instance->m_objects))
    {
        res.append(obj->name());
    }
    return res;
}

QVariant KaZaManager::setting(QString id) {
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
    }
    return m_instance->m_settings.value(id);
}

QString KaZaManager::appChecksum() {
    QString appChecksum;
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return QString();
    }
    QFile f(m_instance->m_appFilename);
    if (f.open(QFile::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Algorithm::Md5);
        if (hash.addData(&f)) {
            appChecksum = hash.result().toBase64();
        }
    }
    return appChecksum;
}

QString KaZaManager::appFilename() {
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return QString();
    }
    return m_instance->m_appFilename;
}

bool KaZaManager::runDbQuery(const QString &query) const
{
    if(!m_databaseReady) return false;
    QSqlQuery q;
    bool res = q.exec(query);
    if(!res)
    {
        qWarning().noquote().nospace() << query << " failed: " << q.lastError().text();
    }
    return res;
}

void KaZaManager::_pendingConnectionAvailable() {
    QTcpSocket *socket = m_server.nextPendingConnection();
    if(socket)
    {
        KaZaConnection *connection = new KaZaConnection(socket, this);
        QObject::connect(connection, &KaZaConnection::disconnectFromHost, this, &KaZaManager::_disconnection);
        m_clients.append(connection);
    }
    else
    {
        qDebug() << "New connection FAILED" << m_server.errorString();
    }
}

void KaZaManager::_disconnection() {
    KaZaConnection *connection = qobject_cast<KaZaConnection*>(QObject::sender());
    if(!connection)
    {
        qWarning() << "Error on disconnect";
        return;
    }
    qDebug().noquote().nospace() << "SSL " << connection->id() << ": Disconnected";
    m_clients.removeAll(connection);
    connection->deleteLater();
}


void KaZaManager::_pendingRemoteConnectionAvailable() {
    QTcpSocket *socket = m_remotecontrol.nextPendingConnection();
    if(socket)
    {
#if DEBUG
        qInfo().noquote() << ">" + socket.localPort() + " connected";
#endif
        KaZaRemoteConnection *connection = new KaZaRemoteConnection(socket, this);
        QObject::connect(connection, &KaZaRemoteConnection::disconnectFromHost, this, &KaZaManager::_remoteDisconnection);
        m_remoteclients.append(connection);
    }
    else
    {
        qDebug() << "New connection FAILED" << m_server.errorString();
    }
}

void KaZaManager::_remoteDisconnection() {
    KaZaRemoteConnection *connection = qobject_cast<KaZaRemoteConnection*>(QObject::sender());
    if(!connection)
    {
        qWarning() << "Error on disconnect";
        return;
    }
    qDebug().noquote().nospace() << "Remote " << connection->id() << ": Disconnected";
    m_remoteclients.removeAll(connection);
    connection->deleteLater();
}

void kaZaRegisterObject(KaZaObject *obj) {
    KaZaManager::registerObject(obj);
}
