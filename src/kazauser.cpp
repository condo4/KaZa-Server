#include "kazauser.h"
#include "kazaconnection.h"
#include "kazaobject.h"

KaZaUser::KaZaUser(QObject *parent)
    : QObject{parent}
    , m_connectedObject{nullptr}
{}

KaZaUser::KaZaUser(const QString &name, QObject *parent)
    : QObject{parent}
    , m_connectedObject{nullptr}
{
    // Create KaZaObject for connected property
    setName(name);
}

void KaZaUser::setName(const QString &name)
{
    if (m_name != name) {
        m_name = name;

        // Create or update KaZaObject for connected property
        if (!m_connectedObject) {
            m_connectedObject = new KaZaObject("user." + m_name.toLower() + ".connected", this);
            m_connectedObject->setValue(connected());
            QObject::connect(this, &KaZaUser::connectedChanged, m_connectedObject, &KaZaObject::setValue);
        } else {
            m_connectedObject->setName("user." + m_name.toLower() + ".connected");
        }

        emit nameChanged();
    }
}

void KaZaUser::addConnection(KaZaConnection *connection)
{
    if (!m_connections.contains(connection)) {
        bool wasConnected = connected();
        m_connections.append(connection);
        if (!wasConnected) {
            emit connectedChanged(true);
        }
    }
}

void KaZaUser::removeConnection(KaZaConnection *connection)
{
    if (m_connections.removeOne(connection)) {
        if (!connected()) {
            emit connectedChanged(false);
        }
    }
}
