#include "kazauser.h"
#include "kazaconnection.h"
#include "kazaobject.h"

KaZaUser::KaZaUser(QObject *parent)
    : QObject{parent}
    , m_connectedObject{nullptr}
    , m_latitudeObject{nullptr}
    , m_longitudeObject{nullptr}
    , m_altitudeObject{nullptr}
    , m_accuracyObject{nullptr}
{}

KaZaUser::KaZaUser(const QString &name, QObject *parent)
    : QObject{parent}
    , m_connectedObject{nullptr}
    , m_latitudeObject{nullptr}
    , m_longitudeObject{nullptr}
    , m_altitudeObject{nullptr}
    , m_accuracyObject{nullptr}
{
    // Create KaZaObject for connected property
    setName(name);
}

void KaZaUser::setName(const QString &name)
{
    if (m_name != name) {
        m_name = name;
        QString baseName = "user." + m_name.toLower();

        // Create or update KaZaObject for connected property
        if (!m_connectedObject) {
            m_connectedObject = new KaZaObject(baseName + ".connected", this);
            m_connectedObject->setValue(connected());
            QObject::connect(this, &KaZaUser::connectedChanged, m_connectedObject, &KaZaObject::setValue);
        } else {
            m_connectedObject->setName(baseName + ".connected");
        }

        // Create or update KaZaObject for latitude
        if (!m_latitudeObject) {
            m_latitudeObject = new KaZaObject(baseName + ".latitude", this);
            m_latitudeObject->setValue(m_latitude);
        } else {
            m_latitudeObject->setName(baseName + ".latitude");
        }

        // Create or update KaZaObject for longitude
        if (!m_longitudeObject) {
            m_longitudeObject = new KaZaObject(baseName + ".longitude", this);
            m_longitudeObject->setValue(m_longitude);
        } else {
            m_longitudeObject->setName(baseName + ".longitude");
        }

        // Create or update KaZaObject for altitude
        if (!m_altitudeObject) {
            m_altitudeObject = new KaZaObject(baseName + ".altitude", this);
            m_altitudeObject->setValue(m_altitude);
        } else {
            m_altitudeObject->setName(baseName + ".altitude");
        }

        // Create or update KaZaObject for accuracy
        if (!m_accuracyObject) {
            m_accuracyObject = new KaZaObject(baseName + ".accuracy", this);
            m_accuracyObject->setValue(m_accuracy);
        } else {
            m_accuracyObject->setName(baseName + ".accuracy");
        }

        emit nameChanged();
    }
}

void KaZaUser::addConnection(KaZaConnection *connection)
{
    if (!m_connections.contains(connection)) {
        bool wasConnected = connected();
        m_connections.append(connection);
        if (m_connections.size() > 100)
            qWarning() << "KaZaUser" << m_name << ": exceeded 100 simultaneous connections — possible leak";
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

void KaZaUser::setPosition(double latitude, double longitude, double altitude, double accuracy)
{
    if (m_latitude != latitude || m_longitude != longitude ||
        m_altitude != altitude || m_accuracy != accuracy) {
        m_latitude = latitude;
        m_longitude = longitude;
        m_altitude = altitude;
        m_accuracy = accuracy;

        // Update KaZaObjects
        if (m_latitudeObject) {
            m_latitudeObject->setValue(m_latitude);
        }
        if (m_longitudeObject) {
            m_longitudeObject->setValue(m_longitude);
        }
        if (m_altitudeObject) {
            m_altitudeObject->setValue(m_altitude);
        }
        if (m_accuracyObject) {
            m_accuracyObject->setValue(m_accuracy);
        }

        emit positionChanged();
    }
}
