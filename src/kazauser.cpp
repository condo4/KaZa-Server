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

void KaZaUser::ensureObject(KaZaObject *&ptr, const QString &fullName, const QVariant &initialValue)
{
    if (!ptr) {
        ptr = new KaZaObject(fullName, this);
        ptr->setValue(initialValue);
    } else {
        ptr->setName(fullName);
    }
}

void KaZaUser::setName(const QString &name)
{
    if (m_name != name) {
        m_name = name;
        QString baseName = "user." + m_name.toLower();

        bool firstTime = !m_connectedObject;
        ensureObject(m_connectedObject, baseName + ".connected", connected());
        if (firstTime)
            QObject::connect(this, &KaZaUser::connectedChanged, m_connectedObject, &KaZaObject::setValue);

        ensureObject(m_latitudeObject,  baseName + ".latitude",  m_latitude);
        ensureObject(m_longitudeObject, baseName + ".longitude", m_longitude);
        ensureObject(m_altitudeObject,  baseName + ".altitude",  m_altitude);
        ensureObject(m_accuracyObject,  baseName + ".accuracy",  m_accuracy);

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
    if (qAbs(m_latitude - latitude) > 1e-7 || qAbs(m_longitude - longitude) > 1e-7 ||
        qAbs(m_altitude - altitude) > 1e-2 || qAbs(m_accuracy - accuracy) > 1e-2) {
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

        emit positionChanged(m_latitude, m_longitude, m_altitude, m_accuracy);
    }
}
