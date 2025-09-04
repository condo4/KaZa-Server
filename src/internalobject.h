#ifndef INTERNALOBJECT_H
#define INTERNALOBJECT_H

#include <QObject>
#include "kazaobject.h"

class InternalObject : public KaZaObject
{
    Q_OBJECT
    bool m_initialized {false};
public:
    explicit InternalObject(QObject *parent = nullptr);

private slots:
    void _initialize();
    void _save();

};

#endif // INTERNALOBJECT_H
