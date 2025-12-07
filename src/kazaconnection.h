#ifndef KAZACONNECTION_H
#define KAZACONNECTION_H

#include <QObject>
#include <QMap>
#include <QVariant>
#include <QAbstractSocket>
#include <QGeoCoordinate>
#include <kazaprotocol.h>


class QTcpSocket;
class KaZaObject;

class KaZaConnection : public QObject
{
    Q_OBJECT
    KaZaProtocol m_protocol;
    QString m_user;
    QString m_devicename;
    int m_channel;
    QString m_idlog;
    QMap<QString, KaZaObject*>  m_obj;
    QMap<QString, quint16>      m_ids;
    QMap<uint16_t, QTcpSocket*> m_sockets;
    bool m_dmzEnabled {false};
    bool m_valid {false};
    QGeoCoordinate m_gpsPosition;
    QString m_gpsProvider;

public:
    explicit KaZaConnection(QTcpSocket *socket, QObject *parent = nullptr);
    quint16 id();
    void sendNotify(QString text);
    void askPosition();
    void sendObjectsList();
    void enableDMZ();
    void subscribeToObject(KaZaObject *obj, quint16 index, bool sendDesc = true);
    bool isDmzEnabled() const { return m_dmzEnabled; }
    QGeoCoordinate gpsPosition() const { return m_gpsPosition; }
    QString gpsProvider() const { return m_gpsProvider; }

    QString user() const;

signals:
    void disconnectFromHost();

private slots:
    // Version negotiation
    void _processVersionNegotiated(QString &username, QString &devicename, int channel);
    void _processVersionIncompatible(QString reason);

    // Regular protocol frames
    void _processFrameSystem(const QString &command);
    void _processFrameObject(quint16 objectId, QVariant value, bool confirm);
    void _processFrameDbQuery(uint32_t queryId, QString query);
    void _processFrameSocketConnect(uint16_t socketId, const QString hostname, uint16_t port);
    void _processFrameSocketData(uint16_t socketId, QByteArray data);
    void _sockReadyRead();
    void _sockStateChange(QAbstractSocket::SocketState state);

    void _objectChanged();
    void _disconnectFromHost();

private:
    QString idlog() const;
};

#endif // KAZACONNECTION_H
