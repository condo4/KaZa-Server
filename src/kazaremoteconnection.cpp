#include "kazaremoteconnection.h"
#include "kazamanager.h"
#include "kazaobject.h"
#include "kazacertificategenerator.h"
#include <QTcpSocket>
#include <QFile>
#include <QSettings>

KaZaRemoteConnection::KaZaRemoteConnection(QTcpSocket *socket, QObject *parent)
    : QObject{parent}
    , m_socket(socket)
{
    QObject::connect(m_socket, &QTcpSocket::readyRead, this, &KaZaRemoteConnection::_dataReady);
    QObject::connect(m_socket, &QTcpSocket::disconnected, this, &KaZaRemoteConnection::disconnectFromHost);
    qDebug().noquote().nospace() << "Control SSL " << id() << ": Connected";
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
    QString cmd = packet.trimmed();
    qDebug().noquote().nospace() << "Control SSL " << id() << ": Command [" << cmd << "]";

    // New protocol: clientconf? adminpass username userpass
    if(cmd.startsWith("clientconf?"))
    {
        QStringList parts = cmd.split(" ");
        if (parts.size() == 4) {
            // New format: clientconf? adminpass username userpass
            QString adminPassword = parts[1];
            QString username = parts[2];
            QString userPassword = parts[3];
            __clientconf(adminPassword, username, userPassword);
        } else {
            // Invalid format
            qWarning().noquote().nospace() << "Control SSL " << id() << ": Invalid clientconf? format. Expected: clientconf? adminpass username userpass";
            m_socket->write("ERROR: Invalid format. Expected: clientconf? adminpass username userpass\n");
            m_socket->disconnectFromHost();
        }
        return;
    }

    // Convert to lowercase for other commands
    cmd = cmd.toLower();

    if(cmd.startsWith("obj?"))
    {
        QStringList args = cmd.split(" ");
        if(args.size() > 1)
        {
            QString key = args[1];
            KaZaObject *obj = KaZaManager::getObject(key);
            if(obj)
            {
                QString line = key.leftJustified(80, ' ');
                line.append(obj->value().toString());
                line.append(" ");
                line.append(obj->unit());
                m_socket->write(line.toUtf8());
                m_socket->write("\n");
            }
            else
            {
                qWarning().noquote().nospace() << "Control SSL " << id() << ": Object not found: " << key;
            }
            m_socket->write("\n");
            return;
        }

        QStringList lst = KaZaManager::getObjectKeys();
        for(QString &key: lst)
        {
            KaZaObject *obj = KaZaManager::getObject(key);
            if(obj)
            {
                QString line = key.leftJustified(80, ' ');
                line.append(obj->value().toString());
                line.append(" ");
                line.append(obj->unit());
                m_socket->write(line.toUtf8());
                m_socket->write("\n");
            }
            else
            {
                qWarning().noquote().nospace() << "Control SSL " << id() << ": Object in key list but not found: " << key;
            }
        }
        m_socket->write("\n");
    }

    if(cmd.startsWith("refresh"))
    {
        QStringList args = cmd.split(" ");
        if(args.size() != 2)
        {
            qWarning().noquote().nospace() << "Control SSL " << id() << ": Invalid refresh query: " << cmd;
            return;
        }
        QString key = args[1];
        KaZaObject *obj = KaZaManager::getObject(key);
        if(obj)
        {
            obj->changeValue(QVariant());
            m_socket->write("OK\n");
        }
        else
        {
            qWarning().noquote().nospace() << "Control SSL " << id() << ": Object not found for refresh: " << key;
            m_socket->write("ERROR: Object not found\n");
        }
        m_socket->write("\n");
    }

    if(cmd.startsWith("notify"))
    {
        KaZaManager::sendNotify(cmd.mid(7));
    }

    if(cmd.startsWith("position?"))
    {
        KaZaManager::askPosition();
    }

}

void KaZaRemoteConnection::__clientconf(const QString &adminPassword, const QString &username, const QString &userPassword) {
    qInfo().noquote().nospace() << "Control SSL " << id() << ": Processing clientconf request for user: " << username;

    // Load server settings
    QSettings settings("/etc/kazad.conf", QSettings::IniFormat);
    QString configPassword = settings.value("control/password").toString();
    QString hostname = settings.value("ssl/hostname").toString();
    QString sslHost = settings.value("Client/host").toString();
    if (sslHost.isEmpty()) {
        sslHost = hostname; // Default to hostname if Client/host not set
    }
    QString sslPort = settings.value("ssl/port").toString();

    // Verify admin password
    if (adminPassword != configPassword) {
        qWarning().noquote().nospace() << "Control SSL " << id() << ": Authentication failed - invalid admin password";
        m_socket->write("ERROR: Authentication failed\n");
        m_socket->disconnectFromHost();
        return;
    }

    qInfo().noquote().nospace() << "Control SSL " << id() << ": Admin authentication successful";

    // Hardcoded paths
    const QString basePath = "/var/lib/kazad";
    QString clientCertPath = basePath + "/" + username + ".cert.pem";
    QString clientKeyPath = basePath + "/" + username + ".key";

    // Check if client certificate already exists
    if (!QFile::exists(clientCertPath) || !QFile::exists(clientKeyPath)) {
        qInfo().noquote().nospace() << "Control SSL " << id() << ": Client certificate not found, generating for user: " << username;

        // Generate client certificate
        if (!KaZaCertificateGenerator::generateClientCertificate(username, userPassword, hostname, basePath)) {
            qCritical().noquote().nospace() << "Control SSL " << id() << ": Failed to generate client certificate for user: " << username;
            m_socket->write("ERROR: Failed to generate client certificate\n");
            m_socket->disconnectFromHost();
            return;
        }

        qInfo().noquote().nospace() << "Control SSL " << id() << ": Client certificate generated successfully for user: " << username;
    } else {
        qInfo().noquote().nospace() << "Control SSL " << id() << ": Using existing client certificate for user: " << username;
    }

    // Send configuration as XML
    m_socket->write("<?xml version='1.0'?>\n");
    m_socket->write("<param>\n");
    m_socket->write("\t<sslhost>" + sslHost.trimmed().toUtf8() + "</sslhost>\n");
    m_socket->write("\t<sslport>" + sslPort.trimmed().toUtf8() + "</sslport>\n");

    // Send client certificate
    m_socket->write("\t<certificate>");
    QFile certificate(clientCertPath);
    if(!certificate.open(QFile::ReadOnly))
    {
        qWarning() << "Can't open client certificate:" << certificate.fileName();
        m_socket->write("ERROR: Failed to read client certificate\n");
        m_socket->disconnectFromHost();
        return;
    }
    m_socket->write(certificate.readAll());
    certificate.close();
    m_socket->write("</certificate>\n");

    // Send client private key
    m_socket->write("\t<key>");
    QFile key(clientKeyPath);
    if(!key.open(QFile::ReadOnly))
    {
        qWarning() << "Can't open client key:" << key.fileName();
        m_socket->write("ERROR: Failed to read client key\n");
        m_socket->disconnectFromHost();
        return;
    }
    m_socket->write(key.readAll());
    key.close();
    m_socket->write("</key>\n");

    // Send CA certificate (hardcoded path)
    m_socket->write("\t<ca>");
    QFile ca(basePath + "/ca.cert.pem");
    if(!ca.open(QFile::ReadOnly))
    {
        qWarning() << "Can't open CA certificate:" << ca.fileName();
        m_socket->write("ERROR: Failed to read CA certificate\n");
        m_socket->disconnectFromHost();
        return;
    }
    m_socket->write(ca.readAll());
    ca.close();
    m_socket->write("</ca>\n");

    m_socket->write("</param>\n");

    qInfo().noquote().nospace() << "Control SSL " << id() << ": Client configuration sent successfully for user: " << username;
}
