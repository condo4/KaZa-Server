#include "kazaconnection.h"
#include "kazamanager.h"
#include "kazaobject.h"
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
            m_protocol.sendObject(m_ids[name], m_obj[name]->value());
        }
        return;
    }

    qWarning().noquote().nospace() << "SSL " << id() << ": Unknown System Command " << command;
}

void KaZaConnection::_objectChanged() {
    KaZaObject *obj = qobject_cast<KaZaObject*>(QObject::sender());
#ifdef DEBUG_CONNECTION
    qDebug() << "_objectChanged " << obj->name() << obj->value();
#endif
    m_protocol.sendObject(m_ids[obj->name()], obj->value());
}

void KaZaConnection::_processFrameObject(quint16 id, QVariant value) {
    KaZaObject *obj = m_obj.value(m_ids.key(id), nullptr);
    if(obj == nullptr)
    {
        qWarning() << "Can't find object with id" << id;
        return;
    }
    obj->changeValue(value);
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
