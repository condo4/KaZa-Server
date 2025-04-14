#include "kazaconnection.h"
#include "kazamanager.h"
#include "kazaobject.h"
#include "kzalarm.h"
#include <QTcpSocket>
#include <QFile>
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

void KaZaConnection::_processFrameSystem(const QString &command) {
    QStringList c = command.split(':');
    if(c[0] == "USER")
    {
        // Register user and send app Checksum
        m_user = c[1];
        qDebug().noquote().nospace() << "SSL " << id() << " " << m_user << " Connected";
        m_protocol.sendCommand("APP:" + KaZaManager::appChecksum());
        return;
    }

    if(c[0] == "APP?")
    {
        // Register user and send app Checksum
#ifdef DEBUG_CONNECTION
        qDebug().noquote().nospace() << "SSL " << id() << ": System Asking application";
#endif
        m_protocol.sendFile("APP", KaZaManager::appFilename());
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
        QString result = "<?xml version='1.0'?>\n";
        result.append("<alarms>");
        for(const KzAlarm *alarm: KaZaManager::alarms())
        {
            if(!alarm->enable())
                continue;
            result.append("<alarm>");
            result.append("<title>");
            result.append(alarm->title());
            result.append("</title>");
            result.append("<message>");
            result.append(alarm->message());
            result.append("</message>");
            result.append("</alarm>");
        }
        result.append("</alarms>");
        QByteArray inputData = result.toUtf8();
        QByteArray compressedData = qCompress(inputData);
        QByteArray base64Data = compressedData.toBase64();
        m_protocol.sendCommand("ALARM:" + QString::fromUtf8(base64Data));
        return;
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

void KaZaConnection::_processFrameObject(quint16 id, QVariant value, bool confirm) {
    KaZaObject *obj = m_obj.value(m_ids.key(id), nullptr);
    if(obj == nullptr)
    {
        qWarning() << "Can't find object with id" << id;
        return;
    }
    obj->changeValue(value, confirm);
}


void KaZaConnection::_processFrameDbQuery(uint32_t id, QString query) {
    QSqlQuery q;
    if(q.exec(query))
    {
        QList<QList<QVariant>> result;
        bool first = true;
        QStringList colums;
        while(q.next())
        {
            QList<QVariant> data;
            for(int i = 0; i < q.record().count(); i++)
            {
                if(first)
                {
                    colums.append(q.record().fieldName(i));
                }
                data.append(q.value(i));
            }
            if(first)
            {
                first = false;
            }
            result.append(data);
        }
        m_protocol.sendDbQueryResult(id, colums, result);
    }
    else
    {
        qWarning() << "QUERY FAIL " + query;
    }
}

void KaZaConnection::_processFrameSocketConnect(uint16_t id, const QString hostname, uint16_t port)
{
    if(m_sockets.contains(id))
    {
        qWarning() << "Socket " << id << "allready used";
        return;
    }
    m_sockets[id] = new QTcpSocket();
    QObject::connect(m_sockets[id], &QTcpSocket::readyRead, this, &KaZaConnection::_sockReadyRead);
    QObject::connect(m_sockets[id], &QTcpSocket::stateChanged, this, &KaZaConnection::_sockStateChange);

    m_sockets[id]->connectToHost(hostname, port);
}

void KaZaConnection::_processFrameSocketData(uint16_t id, QByteArray data)
{
    if(m_sockets.contains(id))
    {
        m_sockets[id]->write(data);
    }
    else
    {
        qWarning() << "Can't find socket " << id;
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
