#ifndef KAZAOBJECT_H
#define KAZAOBJECT_H

#include <QObject>
#include <QVariant>

class KaZaObject : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY unitChanged FINAL)

public:
    explicit KaZaObject(QObject *parent = nullptr);
    explicit KaZaObject(const QString &name, QObject *parent = nullptr);

    virtual QVariant value() const;
    virtual void setValue(QVariant);
    virtual void changeValue(QVariant, bool confirm = false);

    QString name() const;
    void setName(const QString &newName);

    QString unit() const;
    void setUnit(const QString &newName);

    virtual QVariant rawid() const;

signals:
    void valueChanged();
    void nameChanged();
    void unitChanged();

private:
    QVariant m_value;
    QString m_name;
    QString m_unit;
};

extern void kaZaRegisterObject(KaZaObject *obj);

#endif // KAZAOBJECT_H
