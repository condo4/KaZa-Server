#include "internalobject.h"
#include <QSettings>

InternalObject::InternalObject(QObject *parent)
    : KaZaObject{parent}
{
    QObject::connect(this, &KaZaObject::nameChanged, this, &InternalObject::_initialize);
    QObject::connect(this, &InternalObject::initialValueChanged, this, &InternalObject::_initialize);
    QObject::connect(this, &KaZaObject::valueChanged, this, &InternalObject::_save);
}

void InternalObject::_initialize()
{
    if(!m_initialized && m_initialValue.isValid() && name().size() > 1)
    {
        QSettings settings;
        QVariant previous = settings.value(name());
        if(previous.isValid())
        {
            bool ok = false;
            if(previous.metaType().id() == QMetaType::QString)
            {
                int t = previous.toString().toInt(&ok);
                if(ok)
                {
                    setValue(t);
                }
            }
            if(!ok)
            {
                setValue(previous);
            }
            m_initialized = true;
            return;
        }
        if(m_initialValue.isValid())
        {
            setValue(m_initialValue);
            m_initialized = true;
            return;
        }
    }
}

void InternalObject::_save()
{
    if(m_initialized)
    {
        QSettings settings;
        settings.setValue(name(), value());
    }
}

QVariant InternalObject::initialValue() const
{
    return m_initialValue;
}

void InternalObject::setInitialValue(const QVariant &newInitialValue)
{
    if (m_initialValue == newInitialValue)
        return;
    m_initialValue = newInitialValue;
    emit initialValueChanged();
}
