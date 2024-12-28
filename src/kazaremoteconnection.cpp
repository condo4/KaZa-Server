#include "kazaremoteconnection.h"
#include "kazamanager.h"
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
    qDebug().noquote().nospace() << "Remote " << id() << ": Command " << packet;

    if(packet == "clientconf?")
    {
        __clientconf();
    }
}

void KaZaRemoteConnection::__clientconf() {
    m_socket->write("<?xml version='1.0'?>\n");
    m_socket->write("<param>\n");
    m_socket->write("\t<sslhost>" + KaZaManager::setting("client/host").toString().trimmed().toUtf8() + "</sslhost>\n");
    m_socket->write("\t<sslport>" + KaZaManager::setting("ssl/port").toString().trimmed().toUtf8() + "</sslport>\n");
    m_socket->write("\t<certificate>");
    QFile certificate(KaZaManager::setting("client/cert_file").toString());
    certificate.open(QFile::ReadOnly);
    m_socket->write(certificate.readAll());
    certificate.close();
    m_socket->write("</certificate>\n");
    m_socket->write("\t<key>");
    QFile key(KaZaManager::setting("client/private_key_file").toString());
    key.open(QFile::ReadOnly);
    m_socket->write(key.readAll());
    key.close();
    m_socket->write("</key>\n");
    m_socket->write("\t<ca>");
    QFile ca(KaZaManager::setting("ssl/ca_file").toString());
    ca.open(QFile::ReadOnly);
    m_socket->write(ca.readAll());
    ca.close();
    m_socket->write("</ca>\n");
    m_socket->write("</param>\n");
    qDebug().noquote().nospace() << "Remote " << id() << ": Command __clientconf finished";
}
