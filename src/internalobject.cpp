#include "internalobject.h"
#include <QSettings>

InternalObject::InternalObject(QObject *parent)
    : KaZaObject{parent}
{
    QObject::connect(this, &KaZaObject::nameChanged, this, &InternalObject::_initialize);
    QObject::connect(this, &KaZaObject::valueChanged, this, &InternalObject::_save);
}

void InternalObject::_initialize()
{
    QSettings settings;
    QVariant previous = settings.value(name());
    if(previous.isValid())
    {
        setValue(previous);
    }
    m_initialized = true;
}

void InternalObject::_save()
{
    if(m_initialized)
    {
        QSettings settings;
        settings.setValue(name(), value());
    }
}
