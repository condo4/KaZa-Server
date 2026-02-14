#include "kazamanager.h"
#include "kazaconnection.h"
#include "kazaremoteconnection.h"
#include "kazaobject.h"
#include "kazaelement.h"
#include "kzobject.h"
#include "scheduler.h"
#include "kzalarm.h"
#include "internalobject.h"
#include "kazacertificategenerator.h"

#include <QUrl>
#include <QQmlContext>
#include <QFile>
#include <QDir>
#include <QSslKey>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

KaZaManager *KaZaManager::m_instance = {nullptr};

bool KaZaManager::ensureCertificatesExist()
{
    // Check if /var/lib/kazad exists
    QDir kazadDir("/var/lib/kazad");
    if (!kazadDir.exists()) {
        qInfo() << "Creating /var/lib/kazad directory...";
        if (!QDir().mkpath("/var/lib/kazad")) {
            qCritical() << "Failed to create /var/lib/kazad directory";
            return false;
        }
    }

    // Hardcoded certificate paths
    const QString caFile = "/var/lib/kazad/ca.cert.pem";
    const QString certFile = "/var/lib/kazad/server.cert.pem";
    const QString keyFile = "/var/lib/kazad/server.key";

    // Check if all required certificate files exist
    if (QFile::exists(caFile) && QFile::exists(certFile) && QFile::exists(keyFile)) {
        qInfo() << "SSL certificates already exist";
        return true;
    }

    // Certificates don't exist, generate them
    qInfo() << "SSL certificates not found, generating new certificates...";
    QString hostname = m_settings.value("ssl/hostname").toString();
    QString keyPassword = m_settings.value("ssl/keypassword").toString();

    if (hostname.isEmpty()) {
        qCritical() << "ssl/hostname not configured in /etc/kazad.conf";
        return false;
    }

    if (keyPassword.isEmpty()) {
        qCritical() << "ssl/keypassword not configured in /etc/kazad.conf";
        return false;
    }

    return KaZaCertificateGenerator::generateCertificates(hostname, keyPassword, "/var/lib/kazad");
}

KaZaManager::KaZaManager(QObject *parent)
    : QObject{parent}
    , m_settings("/etc/kazad.conf", QSettings::Format::IniFormat)
{
    m_instance = this;

    // Ensure SSL certificates exist, generate if needed
    if (!ensureCertificatesExist()) {
        qCritical() << "Failed to ensure SSL certificates exist - server cannot start";
        return;
    }

    QString qmlconf = m_settings.value("qml/server").toString();

    qmlRegisterType<KaZaObject>("org.kazoe.kaza", 1, 0, "KaZaObject");
    qmlRegisterType<KaZaElement>("org.kazoe.kaza", 1, 0, "KaZaElement");
    qmlRegisterType<KaZaObject>("org.kazoe.kaza", 1, 0, "VariableObject");
    qmlRegisterType<InternalObject>("org.kazoe.kaza", 1, 0, "InternalObject");
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

    // Hardcoded certificate paths
    QString cafile = "/var/lib/kazad/ca.cert.pem";
    QString certfile = "/var/lib/kazad/server.cert.pem";
    QString keyfile = "/var/lib/kazad/server.key";

    QSslConfiguration configuration;
    configuration.setCaCertificates(QSslCertificate::fromPath(cafile));
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    QList<QSslCertificate> certfilelist = QSslCertificate::fromPath(certfile);
    if (certfilelist.isEmpty()) {
        qWarning() << "Failed to load server certificate from:" << certfile;
        return;
    }
    configuration.setLocalCertificate(certfilelist.first());

    QFile key(keyfile);
    if(!key.open(QFile::ReadOnly)) {
        qWarning() << "Couldn't open private key:" << key.errorString();
        return;
    }
    QSslKey sslkey(&key, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, m_settings.value("ssl/keypassword").toByteArray());
    configuration.setPrivateKey(sslkey);
    m_server.setSslConfiguration(configuration);

    QObject::connect(&m_server, &QSslServer::pendingConnectionAvailable, this, &KaZaManager::_pendingConnectionAvailable);
    QObject::connect(&m_server, &QSslServer::errorOccurred, [this](QSslSocket *socket, QAbstractSocket::SocketError socketError){
        qWarning() << "SSL ERROR" << socketError;
    });
    QObject::connect(&m_server, &QSslServer::peerVerifyError, [this](QSslSocket *socket, const QSslError &error){
        qDebug() << "SSL peerVerifyError " << error;
    });

    m_server.listen(QHostAddress::Any, m_settings.value("ssl/port").toInt());
    qInfo() << "Start SSL server, listen on"  << m_server.serverPort();

    // Control/Remote connection server (for certificate distribution)
    // Uses SSL but without client certificate requirement (VerifyNone)
    bool controlEnable = m_settings.value("control/enable", true).toBool();
    if (controlEnable) {
        // Configure SSL for control port (same certs as main server)
        QSslConfiguration controlConfig;
        controlConfig.setLocalCertificate(configuration.localCertificate());
        controlConfig.setPrivateKey(configuration.privateKey());
        controlConfig.setPeerVerifyMode(QSslSocket::VerifyNone);  // No client cert required
        m_remotecontrol.setSslConfiguration(controlConfig);

        QObject::connect(&m_remotecontrol, &QSslServer::pendingConnectionAvailable, this, &KaZaManager::_pendingRemoteConnectionAvailable);
        QObject::connect(&m_remotecontrol, &QSslServer::errorOccurred, [this](QSslSocket *socket, QAbstractSocket::SocketError socketError){
            qWarning() << "Control SSL ERROR" << socketError;
        });

        m_remotecontrol.listen(QHostAddress::Any, m_settings.value("control/port").toInt());
        qInfo() << "Start SSL Control server, listen on"  << m_remotecontrol.serverPort();
    } else {
        qInfo() << "Control server disabled (control/enable=false)";
    }

    /* Calculate current App Checksum */
    m_appFilename = m_settings.value("qml/client").toString();

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

    // Mark initialization as successful
    m_initialized = true;
    qInfo() << "KaZa Server initialized successfully";
}

KaZaManager *KaZaManager::getInstance() {
    return m_instance;
}

void KaZaManager::registerObject(KaZaObject* obj) {
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return;
    }
    m_instance->m_objects.append(obj);
    emit m_instance->objectAdded();

    // Subscribe DMZ connections to new object
    quint16 nextIndex = m_instance->m_objects.size();
    for (KaZaConnection* conn : m_instance->m_clients) {
        if (conn && conn->isDmzEnabled()) {
            conn->subscribeToObject(obj, nextIndex);
        }
    }
}

void KaZaManager::registerAlarm(KzAlarm *obj)
{
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return;
    }
    m_instance->m_alarms.append(obj);
    emit m_instance->alarmAdded();
}

const QList<KzAlarm *> &KaZaManager::alarms()
{
    static QList<KzAlarm *> emptyList;
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return emptyList;
    }
    return m_instance->m_alarms;
}

KaZaObject* KaZaManager::getObject(const QString &name) {
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return nullptr;
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
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return res;
    }
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
        return QVariant();
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

void KaZaManager::sendNotify(QString text)
{
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return;
    }
    qInfo() << "NOTIFICATION: " << text;

    // Check if text starts with "/" for user filtering
    QStringList targetUsers;
    QString message = text;

    if (text.startsWith("/"))
    {
        // Extract user names and actual message
        QStringList parts = text.split(' ', Qt::SkipEmptyParts);

        for (const QString& part : parts)
        {
            if (part.startsWith("/"))
            {
                // Remove the leading "/" and add to target users list
                QString username = part.mid(1); // Remove first character "/"
                if (!username.isEmpty())
                {
                    targetUsers.append(username.toLower());
                }
            }
            else
            {
                // Found the start of the actual message
                // Reconstruct the message from this point
                int messageStart = text.indexOf(part);
                message = text.mid(messageStart);
                break;
            }
        }
    }

    // Send notification to appropriate connections
    for(KaZaConnection* conn: m_instance->m_clients)
    {
        // If targetUsers is empty, send to all (no user filtering)
        // Otherwise, only send if this connection's user is in the target list
        if (targetUsers.isEmpty() || targetUsers.contains(conn->user().toLower()))
        {
            conn->sendNotify(message);
        }
    }
}

void KaZaManager::askPosition(QString param)
{
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return;
    }

    // Check if text starts with "/" for user filtering
    QStringList targetUsers;

    if (param.startsWith("/"))
    {
        // Extract user names and actual message
        QStringList parts = param.split(' ', Qt::SkipEmptyParts);

        for (const QString& part : parts)
        {
            if (part.startsWith("/"))
            {
                // Remove the leading "/" and add to target users list
                QString username = part.mid(1); // Remove first character "/"
                if (!username.isEmpty())
                {
                    targetUsers.append(username.toLower());
                }
            }
        }
    }

    for(KaZaConnection* conn: m_instance->m_clients)
    {
        if (targetUsers.isEmpty() || targetUsers.contains(conn->user().toLower()))
        {
            conn->askPosition();
        }
    }
}

void KaZaManager::sendObjectsList()
{
    if(!m_instance)
    {
        qWarning() << "No KaZaManager object";
        return;
    }
    for(KaZaConnection* conn: m_instance->m_clients)
    {
        conn->sendObjectsList();
    }
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

void KaZaManager::notify(QString message)
{
    sendNotify(message);
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
    m_remoteclients.removeAll(connection);
    connection->deleteLater();
}

void kaZaRegisterObject(KaZaObject *obj) {
    KaZaManager::registerObject(obj);
}
