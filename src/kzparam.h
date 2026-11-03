#ifndef KZPARAM_H
#define KZPARAM_H

#include <QObject>
#include <QStringList>
#include <QVariant>

// Declaration of one parameter of a KzService (see doc/08-services.md, section 5.1).
// Values sent by clients are never trusted: validate() converts them to the declared type
// and enforces the bounds, the result is what gets bound to the SQL statement.
class KzParam : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed FINAL)
    Q_PROPERTY(Type type READ type WRITE setType NOTIFY changed FINAL)
    Q_PROPERTY(bool required READ required WRITE setRequired NOTIFY changed FINAL)
    Q_PROPERTY(QVariant defaultValue READ defaultValue WRITE setDefaultValue NOTIFY changed FINAL)
    Q_PROPERTY(QVariant min READ min WRITE setMin NOTIFY changed FINAL)
    Q_PROPERTY(QVariant max READ max WRITE setMax NOTIFY changed FINAL)
    Q_PROPERTY(QStringList values READ values WRITE setValues NOTIFY changed FINAL)
    Q_PROPERTY(int maxLength READ maxLength WRITE setMaxLength NOTIFY changed FINAL)

public:
    enum Type {
        Int,
        Double,
        Bool,
        String,
        DateTime,   // UTC
        Enum,       // value bound to the statement, must belong to values
        Identifier  // value substituted for %name% in the statement, must belong to values
    };
    Q_ENUM(Type)

    explicit KzParam(QObject *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; emit changed(); }
    Type type() const { return m_type; }
    void setType(Type type) { m_type = type; emit changed(); }
    bool required() const { return m_required; }
    void setRequired(bool required) { m_required = required; emit changed(); }
    QVariant defaultValue() const { return m_default; }
    void setDefaultValue(const QVariant &value) { m_default = value; emit changed(); }
    QVariant min() const { return m_min; }
    void setMin(const QVariant &value) { m_min = value; emit changed(); }
    QVariant max() const { return m_max; }
    void setMax(const QVariant &value) { m_max = value; emit changed(); }
    QStringList values() const { return m_values; }
    void setValues(const QStringList &values) { m_values = values; emit changed(); }
    int maxLength() const { return m_maxLength; }
    void setMaxLength(int length) { m_maxLength = length; emit changed(); }

    static constexpr int DefaultMaxLength = 256;

    // Checks the declaration itself (name, closed list for Enum/Identifier, valid default...).
    bool checkDeclaration(QString *error) const;

    // Converts and checks a client value. `value` may be invalid (absent). On success `out` is
    // the value to bind (or to substitute for Identifier); a null QVariant means SQL NULL.
    bool validate(const QVariant &value, QVariant *out, QString *error) const;

    static QString typeName(Type type);

signals:
    void changed();

private:
    bool convert(const QVariant &in, QVariant *out, QString *error) const;

    QString m_name;
    Type m_type {String};
    bool m_required {false};
    QVariant m_default;
    QVariant m_min;
    QVariant m_max;
    QStringList m_values;
    int m_maxLength {0};
};

#endif // KZPARAM_H
