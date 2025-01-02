#ifndef KZOBJECT_H
#define KZOBJECT_H

#include <QObject>
#include <QVariant>

class KaZaObject;

class KzObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString object READ object WRITE setObject NOTIFY objectChanged FINAL)
    Q_PROPERTY(QVariant value READ value NOTIFY valueChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY unitChanged)

public:
    explicit KzObject(QObject *parent = nullptr);

    QString object() const;
    QVariant value() const;
    QString unit() const;

public slots:
    void setObject(const QString &newObject);
    void set(const QVariant &newValue);
    void refresh();
    void connectObject();

signals:
    void objectChanged();
    void valueChanged();
    void unitChanged();
    void changeRequested(QVariant newData);

private:
    QString m_object;
    QString m_unit;
    QVariant m_value;
    KaZaObject *m_kazaobj {nullptr};
};

#endif // KZOBJECT_H
