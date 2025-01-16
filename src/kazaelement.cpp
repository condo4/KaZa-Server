#include "kazaelement.h"


KaZaElement::KaZaElement(QObject *parent)
    : QObject{parent}
{

}

QQmlListProperty<QObject> KaZaElement::data()
{
    return QQmlListProperty<QObject>(this, &m_objects);
}

