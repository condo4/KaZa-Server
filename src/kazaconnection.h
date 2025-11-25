#ifndef KAZACONNECTION_H
#define KAZACONNECTION_H

#include <QObject>
#include <QMap>
#include <QVariant>
#include <QAbstractSocket>
#include <kazaprotocol.h>


class QTcpSocket;
class KaZaObject;

class KaZaConnection : public QObject
{
    Q_OBJECT
    KaZaProtocol m_protocol;
    QString m_user;
    QMap<QString, KaZaObject*>  m_obj;
    QMap<QString, quint16>      m_ids;
    QMap<uint16_t, QTcpSocket*> m_sockets;

public:
    explicit KaZaConnection(QTcpSocket *socket, QObject *parent = nullptr);
    quint16 id();
    void sendNotify(QString text);
    void askPositions();

signals:
    void disconnectFromHost();

private slots:
    void _processFrameSystem(const QString &command);
    void _processFrameObject(quint16 id, QVariant value, bool confirm);
    void _processFrameDbQuery(uint32_t id, QString query);
    void _processFrameSocketConnect(uint16_t id, const QString hostname, uint16_t port);
    void _processFrameSocketData(uint16_t id, QByteArray data);
    void _sockReadyRead();
    void _sockStateChange(QAbstractSocket::SocketState state);

    void _objectChanged();

};

#endif // KAZACONNECTION_H
