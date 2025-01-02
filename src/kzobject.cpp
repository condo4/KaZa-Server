#include "kzobject.h"
#include "kazaobject.h"
#include "kazamanager.h"

KzObject::KzObject(QObject *parent)
    : QObject{parent}
{
    QObject::connect(KaZaManager::getInstance(), &KaZaManager::objectAdded, this, &KzObject::connectObject);
}

QString KzObject::object() const
{
    if(!m_kazaobj) return QString();
    return m_kazaobj->name();
}

void KzObject::setObject(const QString &newObject)
{
    if (m_object == newObject)
        return;
    m_object = newObject;
    connectObject();
}

QVariant KzObject::value() const
{
    if(!m_kazaobj) return QVariant();
    return m_kazaobj->value();
}

QString KzObject::unit() const
{
    if(!m_kazaobj) return QString();
    return m_kazaobj->unit();
}


void KzObject::set(const QVariant &newValue) {
    emit changeRequested(newValue);
}

void KzObject::refresh()
{
    if(!m_kazaobj) return;
    return m_kazaobj->changeValue(QVariant());
}

void KzObject::connectObject()
{
    if(!m_kazaobj)
    {
        m_kazaobj = KaZaManager::getObject(m_object);
        if(m_kazaobj)
        {
            QObject::connect(m_kazaobj, &KaZaObject::unitChanged, this, &KzObject::unitChanged);
            QObject::connect(m_kazaobj, &KaZaObject::valueChanged, this, &KzObject::valueChanged);
            QObject::connect(this, &KzObject::changeRequested, m_kazaobj, &KaZaObject::changeValue);
            emit valueChanged();
            qDebug() << "Object " << m_object << "ready";
            QObject::disconnect(KaZaManager::getInstance(), &KaZaManager::objectAdded, this, &KzObject::connectObject);
        }
    }
}
