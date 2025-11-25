#include "kazaremoteconnection.h"
#include "kazamanager.h"
#include "kazaobject.h"
#include <QTcpSocket>
#include <QFile>

KaZaRemoteConnection::KaZaRemoteConnection(QTcpSocket *socket, QObject *parent)
    : QObject{parent}
    , m_socket(socket)
{
    QObject::connect(m_socket, &QTcpSocket::readyRead, this, &KaZaRemoteConnection::_dataReady);
    QObject::connect(m_socket, &QTcpSocket::disconnected, this, &KaZaRemoteConnection::disconnectFromHost);
    qDebug().noquote().nospace() << "Remote " << id() << ": Connected";
}


quint16 KaZaRemoteConnection::id() {
    return m_socket->peerPort();
}

void KaZaRemoteConnection::_dataReady()
{
    bool datatoprocess = true;
    QByteArray data = m_socket->readAll();
    m_pending.append(data);

    while(m_pending.contains("\n"))
    {
        uint32_t len = m_pending.indexOf("\n");
        QByteArray data(m_pending.mid(0, len));
        _processPacket(data);
        m_pending.remove(0, len + 1);
    }
}



void KaZaRemoteConnection::_processPacket(const QByteArray &packet) {
    QString cmd = packet.toLower().trimmed();
    qDebug().noquote().nospace() << "Remote " << id() << ": Command [" << cmd << "]";

    if(cmd == "clientconf?")
    {
        __clientconf();
        return;
    }

    if(cmd.startsWith("obj?"))
    {
        QStringList args = cmd.split(" ");
        if(args.size() > 1)
        {
            QString key = args[1];
            KaZaObject *obj = KaZaManager::getObject(key);
            QString line = key.leftJustified(80, ' ');
            line.append(obj->value().toString());
            line.append(" ");
            line.append(obj->unit());
            m_socket->write(line.toUtf8());
            m_socket->write("\n");
            m_socket->write("\n");
            return;
        }

        QStringList lst = KaZaManager::getObjectKeys();
        for(QString &key: lst)
        {
            QString line = key.leftJustified(80, ' ');
            KaZaObject *obj = KaZaManager::getObject(key);
            if(obj)
            {
                line.append(obj->value().toString());
                line.append(" ");
                line.append(obj->unit());
            }
            m_socket->write(line.toUtf8());
            m_socket->write("\n");
        }
        m_socket->write("\n");
    }

    if(cmd.startsWith("refresh"))
    {
        QStringList args = cmd.split(" ");
        if(args.size() != 2)
        {
            qWarning() << "Invalid query" << cmd;
        }
        QString key = args[1];
        KaZaObject *obj = KaZaManager::getObject(key);
        obj->changeValue(QVariant());
        m_socket->write("OK\n");
        m_socket->write("\n");
    }

    if(cmd.startsWith("notify"))
    {
        KaZaManager::sendNotify(cmd.mid(7));
    }

    if(cmd.startsWith("position?"))
    {
        KaZaManager::askPositions();
    }

}

void KaZaRemoteConnection::__clientconf() {
    m_socket->write("<?xml version='1.0'?>\n");
    m_socket->write("<param>\n");
    m_socket->write("\t<sslhost>" + KaZaManager::setting("Client/host").toString().trimmed().toUtf8() + "</sslhost>\n");
    m_socket->write("\t<sslport>" + KaZaManager::setting("ssl/port").toString().trimmed().toUtf8() + "</sslport>\n");
    m_socket->write("\t<certificate>");
    QFile certificate(KaZaManager::setting("ssl/client_cert_file").toString());
    if(!certificate.open(QFile::ReadOnly))
    {
        qWarning() << "Can't open " << certificate.fileName();
    }
    m_socket->write(certificate.readAll());
    certificate.close();
    m_socket->write("</certificate>\n");
    m_socket->write("\t<key>");
    QFile key(KaZaManager::setting("ssl/client_private_key_file").toString());
    if(!key.open(QFile::ReadOnly))
    {
        qWarning() << "Can't open " << key.fileName();
    }
    m_socket->write(key.readAll());
    key.close();
    m_socket->write("</key>\n");
    m_socket->write("\t<ca>");
    QFile ca(KaZaManager::setting("ssl/ca_file").toString());
    if(!ca.open(QFile::ReadOnly))
    {
        qWarning() << "Can't open " << ca.fileName();
    }
    m_socket->write(ca.readAll());
    ca.close();
    m_socket->write("</ca>\n");
    m_socket->write("</param>\n");
    qDebug().noquote().nospace() << "Remote " << id() << ": Command __clientconf finished";
}
