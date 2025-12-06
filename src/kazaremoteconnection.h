#ifndef KAZAREMOTECONNECTION_H
#define KAZAREMOTECONNECTION_H

#include <QObject>
class QTcpSocket;

class KaZaRemoteConnection : public QObject
{
    Q_OBJECT
    QTcpSocket *m_socket;
    QByteArray m_pending;

public:
    explicit KaZaRemoteConnection(QTcpSocket *socket, QObject *parent = nullptr);
    quint16 id();

signals:
    void disconnectFromHost();

private slots:
    void _dataReady();
    void _processPacket(const QByteArray &packet);

private:
    void __clientconf(const QString &adminPassword, const QString &username, const QString &userPassword);

    void _disconnectFromHost();
};

#endif // KAZAREMOTECONNECTION_H
