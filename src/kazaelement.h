#ifndef KAZAELEMENT_H
#define KAZAELEMENT_H

#include <QObject>
#include <QQmlListProperty>

class KaZaElementPrivate;

class KaZaElement : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<QObject> data READ data FINAL)
    Q_CLASSINFO("DefaultProperty", "data")

public:
    explicit KaZaElement(QObject *parent = nullptr);

    QQmlListProperty<QObject> data();

private:
    QList<QObject *> m_objects;
};

#endif // KAZAELEMENT_H

