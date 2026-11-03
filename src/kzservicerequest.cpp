#include "kzservicerequest.h"
#include "kazaservicemanager.h"

#include <QRegularExpression>

KzServiceRequest::KzServiceRequest(KaZaServiceManager *manager, quint64 key, const QString &service,
                                   const QString &user, const QVariantMap &params, int maxRows)
    : QObject{nullptr}      // no parent: owned by the JavaScript engine once handed to the handler
    , m_manager(manager)
    , m_key(key)
    , m_service(service)
    , m_user(user)
    , m_params(params)
    , m_maxRows(maxRows)
{
}

void KzServiceRequest::invalidate()
{
    if(!m_finished)
    {
        m_finished = true;
        emit finishedChanged();
    }
}

void KzServiceRequest::finish(quint8 status, const QString &code, const QString &message, quint8 kind,
                              const QStringList &columns, const QList<QList<QVariant>> &rows,
                              const QVariant &value, bool truncated)
{
    if(m_finished)
        return;
    m_finished = true;
    emit finishedChanged();
    if(!m_manager)
        return;
    KaZaProtocol::ServiceResponse r;
    r.status = status;
    r.errorCode = code;
    r.errorMessage = message;
    r.kind = kind;
    r.columns = columns;
    r.rows = rows;
    r.value = value;
    r.truncated = truncated;
    m_manager->respondScripted(m_key, r);
}

void KzServiceRequest::respondTable(const QStringList &columns, const QVariantList &rows)
{
    QList<QList<QVariant>> table;
    table.reserve(qMin<qsizetype>(rows.size(), m_maxRows));
    bool truncated = false;
    qint64 bytes = 0;
    for(const QVariant &row : rows)
    {
        if(table.size() >= m_maxRows)
        {
            truncated = true;
            break;
        }
        if(row.typeId() != QMetaType::QVariantList)
        {
            qWarning().noquote() << "Service" << m_service << ": respondTable rows must be arrays";
            finish(KaZaProtocol::ServiceInternal, QStringLiteral("internal"), QStringLiteral("internal error"));
            return;
        }
        const QVariantList cells = row.toList();
        for(const QVariant &c : cells)
            bytes += 16 + (c.typeId() == QMetaType::QString ? c.toString().size() * 2 : 0);
        if(bytes > 12 * 1024 * 1024)
        {
            finish(KaZaProtocol::ServiceTooLarge, QStringLiteral("too_large"), QStringLiteral("result too large"));
            return;
        }
        table.append(cells);
    }
    finish(KaZaProtocol::ServiceOk, QString(), QString(), KaZaProtocol::ServiceKindTable, columns, table, QVariant(), truncated);
}

void KzServiceRequest::respondValue(const QVariant &value)
{
    finish(KaZaProtocol::ServiceOk, QString(), QString(), KaZaProtocol::ServiceKindValue, {}, {}, value);
}

void KzServiceRequest::respondEmpty()
{
    finish(KaZaProtocol::ServiceOk, QString(), QString());
}

void KzServiceRequest::fail(const QString &code)
{
    static const QRegularExpression codeRe(QStringLiteral("^[a-z0-9_]{1,32}$"));
    finish(KaZaProtocol::ServiceInternal, codeRe.match(code).hasMatch() ? code : QStringLiteral("internal"),
           QStringLiteral("request failed"));
}

void KzServiceRequest::invalid(const QString &param, const QString &message)
{
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9_]{1,64}$"));
    finish(KaZaProtocol::ServiceInvalidParams, nameRe.match(param).hasMatch() ? param : QString(),
           message.left(200));
}
