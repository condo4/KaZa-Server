#include "kzservice.h"
#include "kazaservicemanager.h"

#include <QJsonArray>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QDebug>

KzService::KzService(QObject *parent)
    : QObject{parent}
{
}

KzService::~KzService()
{
    if(m_compiled)
        KaZaServiceManager::unregisterService(this);
}

QQmlListProperty<KzParam> KzService::params()
{
    return QQmlListProperty<KzParam>(this, nullptr, &KzService::paramAppend, &KzService::paramCount,
                                     &KzService::paramAt, &KzService::paramClear);
}

qsizetype KzService::paramCount(QQmlListProperty<KzParam> *list)
{
    return static_cast<KzService *>(list->object)->m_params.size();
}

KzParam *KzService::paramAt(QQmlListProperty<KzParam> *list, qsizetype index)
{
    return static_cast<KzService *>(list->object)->m_params.at(index);
}

void KzService::paramAppend(QQmlListProperty<KzParam> *list, KzParam *param)
{
    static_cast<KzService *>(list->object)->addParam(param);
}

void KzService::paramClear(QQmlListProperty<KzParam> *list)
{
    static_cast<KzService *>(list->object)->m_params.clear();
}

void KzService::addParam(KzParam *param)
{
    if(!param)
        return;
    if(!param->parent())
        param->setParent(this);
    m_params.append(param);
}

const KzParam *KzService::findParam(const QString &name) const
{
    for(const KzParam *p : m_params)
    {
        if(p->name() == name)
            return p;
    }
    return nullptr;
}

void KzService::componentComplete()
{
    if(!m_scriptEngine)
        m_scriptEngine = qjsEngine(this);
    QString error;
    if(!compile(&error))
        qCritical().noquote() << "KzService" << m_name << "not registered:" << error;
}

bool KzService::allows(const QString &user) const
{
    return m_users.contains(QStringLiteral("*")) || m_users.contains(user);
}

bool KzService::analyzeStatement(const QString &sql, QString *statementOut, QSet<QString> *bindNames,
                                 QSet<QString> *identNames, QString *error)
{
    // Single statement, no comments, no dollar quoting. Quoted literals and identifiers are
    // blanked out so that the placeholder scan only sees code.
    QString statement = sql.trimmed();
    while(statement.endsWith(QLatin1Char(';')))
    {
        statement.chop(1);
        statement = statement.trimmed();
    }
    if(statement.isEmpty())
    {
        *error = QStringLiteral("empty sql");
        return false;
    }

    QString code;
    code.reserve(statement.size());
    QChar quote;
    for(qsizetype i = 0; i < statement.size(); ++i)
    {
        const QChar c = statement.at(i);
        if(!quote.isNull())
        {
            if(c == quote)
            {
                if(i + 1 < statement.size() && statement.at(i + 1) == quote)
                {
                    ++i;    // doubled quote: stays inside the literal
                    code.append(QLatin1String("  "));
                    continue;
                }
                quote = QChar();
            }
            code.append(QLatin1Char(' '));
            continue;
        }
        if(c == QLatin1Char('\'') || c == QLatin1Char('"'))
        {
            quote = c;
            code.append(QLatin1Char(' '));
            continue;
        }
        code.append(c);
    }
    if(!quote.isNull())
    {
        *error = QStringLiteral("unterminated quoted string");
        return false;
    }
    if(code.contains(QLatin1Char(';')))
    {
        *error = QStringLiteral("only one statement is allowed");
        return false;
    }
    if(code.contains(QLatin1String("--")) || code.contains(QLatin1String("/*")))
    {
        *error = QStringLiteral("comments are not allowed in sql");
        return false;
    }
    if(code.contains(QLatin1Char('$')) || code.contains(QLatin1Char('?')))
    {
        *error = QStringLiteral("'$' and '?' are not allowed in sql");
        return false;
    }
    static const QRegularExpression startRe(QStringLiteral("^\\s*(select|with)\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    if(!startRe.match(code).hasMatch())
    {
        *error = QStringLiteral("sql must start with SELECT or WITH");
        return false;
    }

    bindNames->clear();
    identNames->clear();
    static const QRegularExpression identRe(QStringLiteral("%([A-Za-z_][A-Za-z0-9_]*)%"));
    for(auto it = identRe.globalMatch(code); it.hasNext();)
        identNames->insert(it.next().captured(1));

    // ':name' that is not part of a '::' cast
    static const QRegularExpression bindRe(QStringLiteral("(?<!:):([A-Za-z_][A-Za-z0-9_]*)"));
    for(auto it = bindRe.globalMatch(code); it.hasNext();)
        bindNames->insert(it.next().captured(1));

    for(const QString &n : std::as_const(*identNames))
    {
        // a %name% inside a quoted literal would be replaced too: refuse the ambiguity
        const QString token = QLatin1Char('%') + n + QLatin1Char('%');
        if(statement.count(token) != code.count(token))
        {
            *error = QStringLiteral("'%1' also appears inside a quoted string").arg(token);
            return false;
        }
    }
    *statementOut = statement;
    return true;
}

bool KzService::compile(QString *error)
{
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    if(m_compiled)
        return true;
    if(!nameRe.match(m_name).hasMatch())
    {
        *error = QStringLiteral("invalid service name");
        return false;
    }

    QSet<QString> seen;
    for(const KzParam *p : std::as_const(m_params))
    {
        if(!p->checkDeclaration(error))
            return false;
        if(seen.contains(p->name()))
        {
            *error = QStringLiteral("duplicate parameter '%1'").arg(p->name());
            return false;
        }
        seen.insert(p->name());
    }

    const bool hasSql = !m_sql.trimmed().isEmpty();
    if(hasSql && isScripted())
    {
        *error = QStringLiteral("declare either 'sql' or 'handler', not both");
        return false;
    }
    if(!hasSql && !isScripted())
    {
        *error = QStringLiteral("needs 'sql' or a callable 'handler'");
        return false;
    }
    if(isScripted() && !m_scriptEngine)
    {
        *error = QStringLiteral("no script engine for the handler");
        return false;
    }

    m_bindNames.clear();
    m_identNames.clear();
    if(hasSql)
    {
        if(!analyzeStatement(m_sql, &m_statement, &m_bindNames, &m_identNames, error))
            return false;

        for(const QString &n : std::as_const(m_bindNames))
        {
            const KzParam *p = findParam(n);
            if(!p)
            {
                *error = QStringLiteral("placeholder ':%1' has no KzParam").arg(n);
                return false;
            }
            if(p->type() == KzParam::Identifier)
            {
                *error = QStringLiteral("parameter '%1' is an identifier, use %%1%").arg(n);
                return false;
            }
        }
        for(const QString &n : std::as_const(m_identNames))
        {
            const KzParam *p = findParam(n);
            if(!p || p->type() != KzParam::Identifier)
            {
                *error = QStringLiteral("placeholder '%%1%' needs a KzParam of type Identifier").arg(n);
                return false;
            }
        }
        for(const KzParam *p : std::as_const(m_params))
        {
            if(p->type() == KzParam::Identifier && !m_identNames.contains(p->name()))
                qWarning().noquote() << "KzService" << m_name << ": identifier parameter" << p->name() << "is never used";
            else if(p->type() != KzParam::Identifier && !m_bindNames.contains(p->name()))
                qWarning().noquote() << "KzService" << m_name << ": parameter" << p->name() << "is never used";
        }
    }
    else
    {
        for(const KzParam *p : std::as_const(m_params))
        {
            if(p->type() == KzParam::Identifier)
            {
                *error = QStringLiteral("parameter '%1': Identifier is only meaningful with 'sql'").arg(p->name());
                return false;
            }
        }
    }

    if(!KaZaServiceManager::registerService(this, error))
        return false;
    m_compiled = true;
    return true;
}

bool KzService::validateParams(const QVariantMap &clientParams, QVariantMap *out,
                               QString *errorParam, QString *errorMessage) const
{
    for(auto it = clientParams.constBegin(); it != clientParams.constEnd(); ++it)
    {
        if(!findParam(it.key()))
        {
            *errorParam = it.key();
            *errorMessage = QStringLiteral("unknown parameter");
            return false;
        }
    }
    out->clear();
    for(const KzParam *p : m_params)
    {
        QVariant value;
        QString error;
        if(!p->validate(clientParams.value(p->name()), &value, &error))
        {
            *errorParam = p->name();
            *errorMessage = error;
            return false;
        }
        out->insert(p->name(), value);
    }
    return true;
}

bool KzService::buildStatement(const QVariantMap &clientParams, QString *sql, QVariantMap *bindings,
                               QString *errorParam, QString *errorMessage) const
{
    QVariantMap values;
    if(!validateParams(clientParams, &values, errorParam, errorMessage))
        return false;

    QString text = m_statement;
    bindings->clear();
    for(const KzParam *p : m_params)
    {
        const QVariant value = values.value(p->name());
        if(p->type() == KzParam::Identifier)
            text.replace(QLatin1Char('%') + p->name() + QLatin1Char('%'), value.toString());
        else if(m_bindNames.contains(p->name()))
            bindings->insert(p->name(), value);
    }
    *sql = text;
    return true;
}

QJsonObject KzService::describe() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), m_name);
    o.insert(QStringLiteral("description"), m_description);
    o.insert(QStringLiteral("maxRows"), maxRows());
    o.insert(QStringLiteral("single"), m_single);
    o.insert(QStringLiteral("scripted"), isScripted());
    QJsonArray params;
    for(const KzParam *p : m_params)
    {
        QJsonObject po;
        po.insert(QStringLiteral("name"), p->name());
        po.insert(QStringLiteral("type"), KzParam::typeName(p->type()));
        po.insert(QStringLiteral("required"), p->required());
        if(p->defaultValue().isValid())
            po.insert(QStringLiteral("default"), QJsonValue::fromVariant(p->defaultValue()));
        if(p->min().isValid())
            po.insert(QStringLiteral("min"), QJsonValue::fromVariant(p->min()));
        if(p->max().isValid())
            po.insert(QStringLiteral("max"), QJsonValue::fromVariant(p->max()));
        if(!p->values().isEmpty())
            po.insert(QStringLiteral("values"), QJsonArray::fromStringList(p->values()));
        if(p->type() == KzParam::String)
            po.insert(QStringLiteral("maxLength"), p->maxLength() > 0 ? p->maxLength() : KzParam::DefaultMaxLength);
        params.append(po);
    }
    o.insert(QStringLiteral("params"), params);
    return o;
}
