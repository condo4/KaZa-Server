#include "kzparam.h"

#include <QDateTime>
#include <QMetaType>
#include <QRegularExpression>
#include <cmath>

KzParam::KzParam(QObject *parent)
    : QObject{parent}
{
}

QString KzParam::typeName(Type type)
{
    switch(type)
    {
    case Int: return QStringLiteral("int");
    case Double: return QStringLiteral("double");
    case Bool: return QStringLiteral("bool");
    case String: return QStringLiteral("string");
    case DateTime: return QStringLiteral("datetime");
    case Enum: return QStringLiteral("enum");
    case Identifier: return QStringLiteral("identifier");
    }
    return QString();
}

static bool isInteger(const QVariant &v)
{
    switch(v.typeId())
    {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::UChar:
    case QMetaType::SChar:
        return true;
    default:
        return false;
    }
}

bool KzParam::convert(const QVariant &in, QVariant *out, QString *error) const
{
    switch(m_type)
    {
    case Int: {
        if(!isInteger(in))
        {
            *error = QStringLiteral("integer expected");
            return false;
        }
        bool ok = false;
        const qlonglong v = in.toLongLong(&ok);
        if(!ok || (in.typeId() == QMetaType::ULongLong && in.toULongLong() > static_cast<qulonglong>(INT64_MAX)))
        {
            *error = QStringLiteral("integer out of range");
            return false;
        }
        if((m_min.isValid() && v < m_min.toLongLong()) || (m_max.isValid() && v > m_max.toLongLong()))
        {
            *error = QStringLiteral("out of bounds");
            return false;
        }
        *out = v;
        return true;
    }
    case Double: {
        if(!isInteger(in) && in.typeId() != QMetaType::Double && in.typeId() != QMetaType::Float)
        {
            *error = QStringLiteral("number expected");
            return false;
        }
        const double v = in.toDouble();
        if(!std::isfinite(v))
        {
            *error = QStringLiteral("finite number expected");
            return false;
        }
        if((m_min.isValid() && v < m_min.toDouble()) || (m_max.isValid() && v > m_max.toDouble()))
        {
            *error = QStringLiteral("out of bounds");
            return false;
        }
        *out = v;
        return true;
    }
    case Bool:
        if(in.typeId() != QMetaType::Bool)
        {
            *error = QStringLiteral("boolean expected");
            return false;
        }
        *out = in.toBool();
        return true;
    case String: {
        if(in.typeId() != QMetaType::QString)
        {
            *error = QStringLiteral("string expected");
            return false;
        }
        const QString s = in.toString();
        if(s.length() > (m_maxLength > 0 ? m_maxLength : DefaultMaxLength))
        {
            *error = QStringLiteral("string too long");
            return false;
        }
        *out = s;
        return true;
    }
    case DateTime: {
        if(in.typeId() != QMetaType::QDateTime)
        {
            *error = QStringLiteral("datetime expected");
            return false;
        }
        const QDateTime dt = in.toDateTime();
        if(!dt.isValid())
        {
            *error = QStringLiteral("invalid datetime");
            return false;
        }
        *out = dt.toUTC();
        return true;
    }
    case Enum:
    case Identifier: {
        if(in.typeId() != QMetaType::QString || !m_values.contains(in.toString()))
        {
            *error = QStringLiteral("value not allowed");
            return false;
        }
        *out = in.toString();
        return true;
    }
    }
    *error = QStringLiteral("unsupported type");
    return false;
}

bool KzParam::validate(const QVariant &value, QVariant *out, QString *error) const
{
    if(value.isValid() && !value.isNull())
        return convert(value, out, error);

    if(m_default.isValid())
    {
        // The default comes from the server QML: convert it too, so that it is bound with the right type.
        // QML hands over numbers as double, accept integral ones for Int.
        QVariant d = m_default;
        if(m_type == Int && d.typeId() == QMetaType::Double && d.toDouble() == std::floor(d.toDouble()))
            d = d.toLongLong();
        if(m_type == DateTime && d.typeId() == QMetaType::QString)
            d = QDateTime::fromString(d.toString(), Qt::ISODate);
        return convert(d, out, error);
    }
    if(m_required)
    {
        *error = QStringLiteral("required");
        return false;
    }
    if(m_type == Identifier)
    {
        *error = QStringLiteral("required");
        return false;
    }
    *out = QVariant();  // SQL NULL
    return true;
}

bool KzParam::checkDeclaration(QString *error) const
{
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if(!nameRe.match(m_name).hasMatch())
    {
        *error = QStringLiteral("invalid parameter name '%1'").arg(m_name);
        return false;
    }
    if(m_type == Enum || m_type == Identifier)
    {
        if(m_values.isEmpty())
        {
            *error = QStringLiteral("parameter '%1': %2 needs a closed list of values").arg(m_name, typeName(m_type));
            return false;
        }
        if(m_type == Identifier)
        {
            static const QRegularExpression identRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));
            for(const QString &v : m_values)
            {
                if(!identRe.match(v).hasMatch())
                {
                    *error = QStringLiteral("parameter '%1': '%2' is not a plain SQL identifier").arg(m_name, v);
                    return false;
                }
            }
        }
    }
    if(m_default.isValid())
    {
        QVariant out;
        QString e;
        if(!validate(QVariant(), &out, &e))
        {
            *error = QStringLiteral("parameter '%1': invalid default (%2)").arg(m_name, e);
            return false;
        }
    }
    return true;
}
