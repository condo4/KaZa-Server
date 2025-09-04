#ifndef INTERNALOBJECT_H
#define INTERNALOBJECT_H

#include <QObject>
#include "kazaobject.h"

class InternalObject : public KaZaObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant initialValue READ initialValue WRITE setInitialValue NOTIFY initialValueChanged)

public:
    explicit InternalObject(QObject *parent = nullptr);

    QVariant initialValue() const;
    void setInitialValue(const QVariant &newInitialValue);

signals:
    void initialValueChanged();

private slots:
    void _initialize();
    void _save();

private:
    bool m_initialized {false};
    QVariant m_initialValue;
};

#endif // INTERNALOBJECT_H
