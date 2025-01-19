#include "kazaobject.h"
#include <QVariant>

KaZaObject::KaZaObject(QObject *parent)
    : QObject{parent}
{
    kaZaRegisterObject(this);
}


KaZaObject::KaZaObject(const QString &name, QObject *parent)
    : QObject{parent}
    , m_name(name)
{
    kaZaRegisterObject(this);
}

QString KaZaObject::name() const {
    return m_name;
}

void KaZaObject::setName(const QString &newName) {
    if(m_name != newName) {
        m_name = newName;
        emit nameChanged();
    }
}

QString KaZaObject::unit() const {
    return m_unit;
}

void KaZaObject::setUnit(const QString &newUnit) {
    if(m_unit != newUnit) {
        m_unit = newUnit;
        emit unitChanged();
    }
}

QVariant KaZaObject::rawid() const
{
    // By default, no data
    return QVariant();
}

QVariant KaZaObject::value() const
{
    return m_value;
}

void KaZaObject::setValue(QVariant newValue)
{
    if(m_value != newValue)
    {
        m_value = newValue;
        emit valueChanged();
    }
}

void KaZaObject::changeValue(QVariant newValue)
{
    KaZaObject::setValue(newValue);
}
