#ifndef KAZACONNECTION_H
#define KAZACONNECTION_H

#include <QObject>
#include <QMap>
#include <QVariant>
#include <kazaprotocol.h>

class QTcpSocket;
class KaZaObject;

class KaZaConnection : public QObject
{
    Q_OBJECT
    KaZaProtocol m_protocol;
    QString m_user;
    QMap<QString, KaZaObject*> m_obj;
    QMap<QString, quint16>     m_ids;

public:
    explicit KaZaConnection(QTcpSocket *socket, QObject *parent = nullptr);
    quint16 id();

signals:
    void disconnectFromHost();

private slots:
    void _processFrameSystem(const QString &command);
    void _processFrameObject(quint16 id, QVariant value);
    void _processFrameDbQuery(uint32_t id, QString query);

    void _objectChanged();

};

#endif // KAZACONNECTION_H
