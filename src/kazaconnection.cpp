#include "kazaconnection.h"
#include "kazamanager.h"
#include "kazaobject.h"
#include "kzalarm.h"
#include <QTcpSocket>
#include <QFile>
#include <QTimer>
#include <QBuffer>
#include <QSqlQuery>
#include <QSqlRecord>


KaZaConnection::KaZaConnection(QTcpSocket *socket, QObject *parent)
    : QObject{parent}
    , m_protocol(socket)
{
#ifdef DEBUG_CONNECTION
    qDebug().noquote().nospace() << "SSL " << id() << ": Connected";
#endif
    // Setup version negotiation signals (server waits for client version)
    QObject::connect(&m_protocol, &KaZaProtocol::versionNegotiated, this, &KaZaConnection::_processVersionNegotiated);
    QObject::connect(&m_protocol, &KaZaProtocol::versionIncompatible, this, &KaZaConnection::_processVersionIncompatible);

    // Setup regular protocol signals
    QObject::connect(&m_protocol, &KaZaProtocol::disconnectFromHost, this, &KaZaConnection::disconnectFromHost);
    QObject::connect(&m_protocol, &KaZaProtocol::frameCommand, this, &KaZaConnection::_processFrameSystem);
    QObject::connect(&m_protocol, &KaZaProtocol::frameOject, this, &KaZaConnection::_processFrameObject);
    QObject::connect(&m_protocol, &KaZaProtocol::frameDbQuery, this, &KaZaConnection::_processFrameDbQuery);
    QObject::connect(&m_protocol, &KaZaProtocol::frameSocketConnect, this, &KaZaConnection::_processFrameSocketConnect);
    QObject::connect(&m_protocol, &KaZaProtocol::frameSocketData, this, &KaZaConnection::_processFrameSocketData);
}

quint16 KaZaConnection::id() {
    return m_protocol.peerPort();
}

void KaZaConnection::sendNotify(QString text) {
    m_protocol.sendCommand("NOTIFY:" + text);
}

void KaZaConnection::askPosition()
{
    m_protocol.sendCommand("POSITION?");
}

void KaZaConnection::sendObjectsList()
{
    // Build QMap from ALL objects in KaZaManager (not just subscribed ones)
    QMap<QString, QPair<QVariant, QString>> objects;
    QStringList objectKeys = KaZaManager::getObjectKeys();

    for (const QString &key : objectKeys) {
        KaZaObject *obj = KaZaManager::getObject(key);
        if (obj) {
            objects[key] = QPair<QVariant, QString>(obj->value(), obj->unit());
        }
    }

    // Send compressed objects list via protocol
    m_protocol.sendFrameObjectsList(objects);
}

void KaZaConnection::enableDMZ()
{
    if (m_dmzEnabled) {
        qInfo().noquote().nospace() << "SSL " << id() << ": DMZ already enabled";
        return;
    }

    qInfo().noquote().nospace() << "SSL " << id() << ": Enabling DMZ - subscribing to all objects";
    m_dmzEnabled = true;

    // Subscribe to all existing objects
    QStringList objectKeys = KaZaManager::getObjectKeys();
    quint16 index = 0;
    for (const QString &name : objectKeys) {
        KaZaObject *obj = KaZaManager::getObject(name);
        if (obj && !m_obj.contains(name)) {
            subscribeToObject(obj, index++, false);
        }
    }

    qInfo().noquote().nospace() << "SSL " << id() << ": DMZ enabled - subscribed to " << m_obj.size() << " objects";
}

void KaZaConnection::subscribeToObject(KaZaObject *obj, quint16 index, bool sendDesc)
{
    if (!obj) return;

    QString name = obj->name();
    if (m_obj.contains(name)) {
        return; // Already subscribed
    }

    m_obj[name] = obj;
    m_ids[name] = index;
    QObject::connect(obj, &KaZaObject::valueChanged, this, &KaZaConnection::_objectChanged);

    // Only send OBJDESC if client hasn't already received the objects list
    // (OBJLIST already contains name, value, and unit for all objects)
    if (sendDesc) {
        m_protocol.sendCommand("OBJDESC:" + name + ":" + obj->unit());
    }

    // Send initial value only if objects list wasn't sent
    // (OBJLIST already contains current values)
    if (sendDesc && obj->value().isValid()) {
        m_protocol.sendObject(index, obj->value(), false);
    }
}


void KaZaConnection::_processVersionNegotiated(QString &username, QString &devicename, int channel)
{
    m_channel = channel;
    m_devicename = devicename;
    m_user = username;
    qInfo().noquote().nospace() << "SSL " << id() << ": " << username << " connected with " << devicename << " for " << channel;
    m_valid = true;

    m_protocol.sendCommand("APP:" + KaZaManager::appChecksum());

    // Send all registered objects to client
    for(const QString &name : m_obj.keys())
    {
        m_protocol.sendObject(m_ids[name], m_obj[name]->value(), false);
    }
}

void KaZaConnection::_processVersionIncompatible(QString reason)
{
    qWarning().noquote().nospace() << "SSL " << id() << ": Version incompatible - " << reason;
    QTimer::singleShot(1000, this, [this]() {
        emit disconnectFromHost();
    });
}

void KaZaConnection::_processFrameSystem(const QString &command) {
    // Version negotiation is mandatory - reject if not negotiated
    if (!m_valid) {
        qWarning().noquote().nospace() << "SSL " << id() << ": Rejecting frame - version not negotiated";
        emit disconnectFromHost();
        return;
    }

    QStringList c = command.split(':');
    if(c[0] == "APP?")
    {
        // Register user and send app Checksum
#ifdef DEBUG_CONNECTION
        qDebug().noquote().nospace() << "SSL " << id() << ": System Asking application";
#endif
        m_protocol.sendFile("APP", KaZaManager::appFilename());
        return;
    }

    if(c[0] == "OBJLIST?")
    {
        // Client requests compressed objects list
#ifdef DEBUG_CONNECTION
        qDebug().noquote().nospace() << "SSL " << id() << ": Client requesting objects list";
#endif
        sendObjectsList();
        return;
    }

    if(c[0] == "DMZ")
    {
        // Enable DMZ mode - subscribe to all objects
        enableDMZ();
        m_protocol.sendCommand("DMZ:OK");
        return;
    }

    if(c[0] == "OBJ")
    {
        // Register object for connection
        QString &name = c[1];
        quint16 index = c[2].toInt();
#ifdef DEBUG_CONNECTION
        qDebug().noquote().nospace() << "SSL " << id() << ": System Register object " << c[1];
#endif
        if(!m_obj.contains(c[1]))
        {
            KaZaObject *obj = KaZaManager::getObject(name);
            if(obj)
            {
                m_obj[name] = obj;
                m_ids[name] = index;
                QObject::connect(m_obj[name], &KaZaObject::valueChanged, this, &KaZaConnection::_objectChanged);
            }
            else
            {
                qWarning() << "Can't find OBJECT " << name;
                return;
            }
        }
        m_protocol.sendCommand("OBJDESC:" + name + ":" + m_obj[name]->unit());
        if(m_obj[name]->value().isValid())
        {
            m_protocol.sendObject(m_ids[name], m_obj[name]->value(), false);
        }
        return;
    }

    if(c[0] == "ALARMS")
    {
        m_user = c[1];
        QString result;
        for(const KzAlarm *alarm: KaZaManager::alarms())
        {
            if(!alarm->enable())
                continue;
            result.append(alarm->title());
            result.append("\n");
            result.append(alarm->message());
            result.append("\n\n");
        }
        QByteArray inputData = result.toUtf8();
        QByteArray compressedData = qCompress(inputData);
        QByteArray base64Data = compressedData.toBase64();
        m_protocol.sendCommand("ALARM:" + QString::fromUtf8(base64Data));
        return;
    }

    if(c[0] == "LISTOBJECTS")
    {
        // Get list of all available objects
        QStringList objectNames = KaZaManager::getObjectKeys();
        QString objectList = objectNames.join(',');

        qInfo().noquote().nospace() << "SSL " << id() << ": Sending list of " << objectNames.size() << " objects";
        m_protocol.sendCommand("LISTOBJECTS:" + objectList);
        return;
    }

    if(c[0] == "PING")
    {
        m_protocol.sendCommand("PONG");
    }

    qWarning().noquote().nospace() << "SSL " << id() << ": Unknown System Command " << command;
}

void KaZaConnection::_objectChanged() {
    KaZaObject *obj = qobject_cast<KaZaObject*>(QObject::sender());
#ifdef DEBUG_CONNECTION
    qDebug() << "_objectChanged " << obj->name() << obj->value();
#endif
    m_protocol.sendObject(m_ids[obj->name()], obj->value(), false);
}

void KaZaConnection::_processFrameObject(quint16 objectId, QVariant value, bool confirm) {
    // Version negotiation is mandatory - reject if not negotiated
    if (!m_valid) {
        qWarning().noquote().nospace() << "SSL " << id() << ": Rejecting frame - version not negotiated";
        emit disconnectFromHost();
        return;
    }

    KaZaObject *obj = m_obj.value(m_ids.key(objectId), nullptr);
    if(obj == nullptr)
    {
        qWarning() << "Can't find object with id" << objectId;
        return;
    }
    obj->changeValue(value, confirm);
}


void KaZaConnection::_processFrameDbQuery(uint32_t queryId, QString query) {
    // Version negotiation is mandatory - reject if not negotiated
    if (!m_valid) {
        qWarning().noquote().nospace() << "SSL " << id() << ": Rejecting frame - version not negotiated";
        emit disconnectFromHost();
        return;
    }

    QSqlQuery q;
    if(q.exec(query))
    {
        QList<QList<QVariant>> result;
        bool first = true;
        QStringList columns;
        while(q.next())
        {
            QList<QVariant> data;
            for(int i = 0; i < q.record().count(); i++)
            {
                if(first)
                {
                    columns.append(q.record().fieldName(i));
                }
                data.append(q.value(i));
            }
            if(first)
            {
                first = false;
            }
            result.append(data);
        }
        m_protocol.sendDbQueryResult(queryId, columns, result);
    }
    else
    {
        qWarning() << "QUERY FAIL " + query;
    }
}

void KaZaConnection::_processFrameSocketConnect(uint16_t socketId, const QString hostname, uint16_t port)
{
    if(m_sockets.contains(socketId))
    {
        qWarning() << "Socket " << socketId << "allready used";
        return;
    }
    m_sockets[socketId] = new QTcpSocket();
    QObject::connect(m_sockets[socketId], &QTcpSocket::readyRead, this, &KaZaConnection::_sockReadyRead);
    QObject::connect(m_sockets[socketId], &QTcpSocket::stateChanged, this, &KaZaConnection::_sockStateChange);

    m_sockets[socketId]->connectToHost(hostname, port);
}

void KaZaConnection::_processFrameSocketData(uint16_t socketId, QByteArray data)
{
    if(m_sockets.contains(socketId))
    {
        m_sockets[socketId]->write(data);
    }
    else
    {
        qWarning() << "Can't find socket " << socketId;
    }
}

void KaZaConnection::_sockReadyRead()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(QObject::sender());
    if(sock)
    {
        uint16_t id = m_sockets.key(sock);
        QByteArray data = sock->readAll();
        m_protocol.sendSocketData(id, data);
    }
    else
    {
        qWarning() << "Can't find socket on _sockReadyRead";
    }
}

void KaZaConnection::_sockStateChange(QAbstractSocket::SocketState state)
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(QObject::sender());
    if(sock)
    {
        uint16_t id = m_sockets.key(sock);
        m_protocol.sendSocketState(id, state);
    }
    else
    {
        qWarning() << "Can't find socket on _sockStateChange";
    }
}
