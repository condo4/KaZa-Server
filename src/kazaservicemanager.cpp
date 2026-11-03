#include "kazaservicemanager.h"
#include "kzservice.h"
#include "kzservicerequest.h"

#include <QJSEngine>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QDebug>

KaZaServiceManager *KaZaServiceManager::s_instance = nullptr;

static QHash<QString, QPointer<KzService>> &registry()
{
    static QHash<QString, QPointer<KzService>> services;
    return services;
}

// ---------------------------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------------------------

KaZaServiceWorker::KaZaServiceWorker(const ServiceDbConfig &config, int index, QObject *parent)
    : QObject{parent}
    , m_config(config)
    , m_index(index)
    , m_connectionName(QStringLiteral("kaza_service_%1").arg(index))
{
}

bool KaZaServiceWorker::ensureOpen()
{
    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
                          ? QSqlDatabase::database(m_connectionName, false)
                          : QSqlDatabase::addDatabase(m_config.driver, m_connectionName);
    if(db.isOpen())
        return true;
    db.setDatabaseName(m_config.dbName);
    db.setHostName(m_config.hostName);
    db.setPort(m_config.port);
    if(!db.open(m_config.userName, m_config.password))
    {
        qWarning().noquote() << "Service worker" << m_index << ": database open error:" << db.lastError().text();
        return false;
    }
    return true;
}

void KaZaServiceWorker::shutdown()
{
    if(QSqlDatabase::contains(m_connectionName))
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            if(db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

void KaZaServiceWorker::execute(const ServiceJob &job)
{
    KaZaProtocol::ServiceResponse response;
    auto fail = [&](quint8 status, const QString &code, const QString &message) {
        response = KaZaProtocol::ServiceResponse();
        response.status = status;
        response.errorCode = code;
        response.errorMessage = message;
    };

    if(!ensureOpen())
    {
        fail(KaZaProtocol::ServiceUnavailable, QStringLiteral("database"), QStringLiteral("database unavailable"));
        emit finished(m_index, job.key, response);
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    const bool postgres = (m_config.driver == QLatin1String("QPSQL"));
    bool inTransaction = false;
    if(postgres)
    {
        QSqlQuery pre(db);
        if(!pre.exec(QStringLiteral("BEGIN READ ONLY"))
            || !pre.exec(QStringLiteral("SET LOCAL statement_timeout = %1").arg(job.timeoutMs)))
        {
            qWarning().noquote() << "Service worker" << m_index << ": cannot start read-only transaction:" << pre.lastError().text();
            if(!pre.exec(QStringLiteral("ROLLBACK")))
                db.close();     // broken connection: reopened on the next job
            fail(KaZaProtocol::ServiceUnavailable, QStringLiteral("database"), QStringLiteral("database unavailable"));
            emit finished(m_index, job.key, response);
            return;
        }
        inTransaction = true;
    }
    else
    {
        qWarning().noquote() << "Service worker: driver" << m_config.driver << "has no read-only transaction support";
    }

    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        bool ok = q.prepare(job.sql);
        if(ok)
        {
            for(auto it = job.bindings.constBegin(); it != job.bindings.constEnd(); ++it)
                q.bindValue(QLatin1Char(':') + it.key(), it.value());
            ok = q.exec();
        }

        if(!ok)
        {
            const QSqlError err = q.lastError();
            // The SQL text and the driver message stay in the journal, the client only gets a code.
            qWarning().noquote() << "Service query failed:" << err.nativeErrorCode() << err.text();
            if(err.nativeErrorCode() == QLatin1String("57014"))
                fail(KaZaProtocol::ServiceTimeout, QStringLiteral("timeout"), QStringLiteral("query timed out"));
            else if(err.type() == QSqlError::ConnectionError)
                fail(KaZaProtocol::ServiceUnavailable, QStringLiteral("database"), QStringLiteral("database unavailable"));
            else
                fail(KaZaProtocol::ServiceInternal, QStringLiteral("internal"), QStringLiteral("internal error"));
        }
        else if(job.single)
        {
            response.kind = KaZaProtocol::ServiceKindValue;
            if(q.next())
                response.value = q.value(0);
        }
        else
        {
            constexpr qint64 maxBytes = 12 * 1024 * 1024;   // keeps the encoded response below the receiver limit
            qint64 bytes = 0;
            const int columns = q.record().count();
            response.kind = KaZaProtocol::ServiceKindTable;
            for(int i = 0; i < columns; ++i)
                response.columns.append(q.record().fieldName(i));
            while(q.next())
            {
                if(response.rows.size() >= job.maxRows)
                {
                    response.truncated = true;
                    break;
                }
                QList<QVariant> row;
                row.reserve(columns);
                for(int i = 0; i < columns; ++i)
                {
                    const QVariant v = q.value(i);
                    bytes += 16 + (v.typeId() == QMetaType::QString ? v.toString().size() * 2 : 0);
                    row.append(v);
                }
                if(bytes > maxBytes)
                {
                    fail(KaZaProtocol::ServiceTooLarge, QStringLiteral("too_large"), QStringLiteral("result too large"));
                    break;
                }
                response.rows.append(row);
            }
        }

        // Roll back before the statement handle is released, otherwise the driver's DEALLOCATE fails
        // inside an aborted transaction.
        if(inTransaction)
        {
            QSqlQuery post(db);
            if(!post.exec(QStringLiteral("ROLLBACK")))
                db.close();     // reopened on the next job
        }
    }

    emit finished(m_index, job.key, response);
}

// ---------------------------------------------------------------------------------------------
// Manager
// ---------------------------------------------------------------------------------------------

KaZaServiceManager::KaZaServiceManager(QObject *parent)
    : QObject{parent}
{
    qRegisterMetaType<ServiceJob>("ServiceJob");
    qRegisterMetaType<KaZaProtocol::ServiceResponse>("KaZaProtocol::ServiceResponse");
    s_instance = this;
}

KaZaServiceManager::~KaZaServiceManager()
{
    for(Worker &w : m_workers)
    {
        QMetaObject::invokeMethod(w.worker, &KaZaServiceWorker::shutdown, Qt::BlockingQueuedConnection);
        w.thread->quit();
        w.thread->wait();
        delete w.worker;
        delete w.thread;
    }
    for(auto it = m_pending.begin(); it != m_pending.end(); ++it)
        delete it->timer;
    if(s_instance == this)
        s_instance = nullptr;
}

KaZaServiceManager *KaZaServiceManager::instance()
{
    return s_instance;
}

bool KaZaServiceManager::registerService(KzService *service, QString *error)
{
    auto &services = registry();
    const QPointer<KzService> existing = services.value(service->name());
    if(existing && existing != service)
    {
        *error = QStringLiteral("service '%1' already declared").arg(service->name());
        return false;
    }
    services.insert(service->name(), service);
    qInfo().noquote() << "Service registered:" << service->name();
    return true;
}

void KaZaServiceManager::unregisterService(KzService *service)
{
    auto &services = registry();
    if(services.value(service->name()) == service)
        services.remove(service->name());
}

void KaZaServiceManager::start(const ServiceDbConfig &config, const Limits &limits)
{
    if(m_started)
        return;
    m_limits = limits;
    m_limits.workers = qBound(1, limits.workers, 16);
    for(int i = 0; i < m_limits.workers; ++i)
    {
        Worker w;
        w.thread = new QThread;
        w.worker = new KaZaServiceWorker(config, i);
        w.worker->moveToThread(w.thread);
        QObject::connect(w.worker, &KaZaServiceWorker::finished, this, &KaZaServiceManager::_workerFinished);
        w.thread->start();
        m_workers.push_back(w);
    }
    m_started = true;
    qInfo().noquote() << "Services: started" << m_limits.workers << "worker(s)";
}

KaZaProtocol::ServiceResponse KaZaServiceManager::error(quint32 id, quint8 status, const QString &code, const QString &message)
{
    KaZaProtocol::ServiceResponse r;
    r.id = id;
    r.status = status;
    r.errorCode = code;
    r.errorMessage = message;
    return r;
}

void KaZaServiceManager::request(QObject *owner, const QString &user, quint32 id, const QString &service,
                                 const QVariantMap &params, const Reply &reply)
{
    if(!m_started)
    {
        reply(error(id, KaZaProtocol::ServiceUnavailable, QStringLiteral("database"), QStringLiteral("services unavailable")));
        return;
    }

    if(!m_byOwner.contains(owner))
    {
        QObject::connect(owner, &QObject::destroyed, this, [this, owner]() { dropOwner(owner); });
    }
    QHash<quint32, quint64> &inflight = m_byOwner[owner];
    if(inflight.contains(id))
    {
        reply(error(id, KaZaProtocol::ServiceInvalidParams, QStringLiteral("id"), QStringLiteral("request id already in use")));
        return;
    }
    if(inflight.size() >= m_limits.maxInFlight || static_cast<int>(m_queue.size()) >= m_limits.queueLimit)
    {
        reply(error(id, KaZaProtocol::ServiceBusy, QStringLiteral("busy"), QStringLiteral("too many requests")));
        return;
    }

    // Unknown and forbidden are deliberately indistinguishable
    KzService *svc = registry().value(service);
    if(!svc || !svc->allows(user))
    {
        qInfo().noquote() << "Service" << service << "refused for" << user;
        reply(error(id, KaZaProtocol::ServiceUnknown, QStringLiteral("unknown_service"), QStringLiteral("unknown service")));
        return;
    }

    QString errorParam;
    QString errorMessage;
    if(svc->isScripted())
    {
        QVariantMap values;
        if(!svc->validateParams(params, &values, &errorParam, &errorMessage))
        {
            qInfo().noquote() << "Service" << service << "invalid parameter" << errorParam << ":" << errorMessage << "from" << user;
            reply(error(id, KaZaProtocol::ServiceInvalidParams, errorParam, errorMessage));
            return;
        }
        const quint64 key = addPending(owner, user, id, service, reply, svc->timeout(), false);
        auto *req = new KzServiceRequest(this, key, service, user, values, svc->maxRows());
        m_pending[key].request = req;
        QJSValue handler = svc->handler();
        QJSEngine *engine = svc->scriptEngine();
        // The request object is owned (and garbage collected) by the JavaScript engine
        QJSValue result = handler.call({engine->newQObject(req)});
        if(result.isError())
        {
            qWarning().noquote() << "Service" << service << "handler failed:" << result.toString()
                                 << "(" << result.property(QStringLiteral("fileName")).toString() << ":"
                                 << result.property(QStringLiteral("lineNumber")).toInt() << ")";
            req->fail(QStringLiteral("internal"));
        }
        return;
    }

    ServiceJob job;
    if(!svc->buildStatement(params, &job.sql, &job.bindings, &errorParam, &errorMessage))
    {
        qInfo().noquote() << "Service" << service << "invalid parameter" << errorParam << ":" << errorMessage << "from" << user;
        reply(error(id, KaZaProtocol::ServiceInvalidParams, errorParam, errorMessage));
        return;
    }
    job.timeoutMs = svc->timeout();
    job.maxRows = svc->maxRows();
    job.single = svc->single();
    job.key = addPending(owner, user, id, service, reply, job.timeoutMs + 1000, false);
    m_queue.push_back(job);
    dispatch();
}

quint64 KaZaServiceManager::addPending(QObject *owner, const QString &user, quint32 id, const QString &service,
                                       const Reply &reply, int timeoutMs, bool internal)
{
    const quint64 key = m_nextKey++;
    Pending p;
    p.owner = owner;
    p.service = service;
    p.user = user;
    p.id = id;
    p.reply = reply;
    p.internal = internal;
    p.clock.start();
    // Whole life of the request (queue + execution). For SQL the callers add a margin so that the
    // database timeout normally answers first with a precise status.
    p.timer = new QTimer(this);
    p.timer->setSingleShot(true);
    QObject::connect(p.timer, &QTimer::timeout, this, [this, key]() {
        auto it = m_pending.find(key);
        if(it == m_pending.end())
            return;
        qWarning().noquote() << "Service" << it->service << "timed out for" << it->user;
        for(auto q = m_queue.begin(); q != m_queue.end(); ++q)
        {
            if(q->key == key)
            {
                m_queue.erase(q);
                break;
            }
        }
        complete(key, error(it->id, KaZaProtocol::ServiceTimeout, QStringLiteral("timeout"), QStringLiteral("query timed out")));
    });
    p.timer->start(timeoutMs);
    m_pending.insert(key, p);
    if(!internal)
        m_byOwner[owner].insert(id, key);
    return key;
}

void KaZaServiceManager::respondScripted(quint64 key, const KaZaProtocol::ServiceResponse &response)
{
    if(m_pending.contains(key))
        complete(key, response);
}

void KaZaServiceManager::queryScript(const QString &sql, const QVariantMap &bindings, const QJSValue &callback,
                                     QJSEngine *engine)
{
    if(!callback.isCallable())
    {
        qWarning() << "kazamanager.query: callback is not a function";
        return;
    }
    runQuery(sql, bindings, KzService::DefaultTimeoutMs, KzService::DefaultMaxRows,
             [engine, callback](const KaZaProtocol::ServiceResponse &r) mutable {
        QVariantMap result;
        result.insert(QStringLiteral("ok"), r.status == KaZaProtocol::ServiceOk);
        result.insert(QStringLiteral("status"), r.status);
        result.insert(QStringLiteral("errorCode"), r.errorCode);
        result.insert(QStringLiteral("columns"), r.columns);
        QVariantList rows;
        rows.reserve(r.rows.size());
        for(const QList<QVariant> &row : r.rows)
            rows.append(QVariant(QVariantList(row.cbegin(), row.cend())));
        result.insert(QStringLiteral("rows"), rows);
        result.insert(QStringLiteral("truncated"), r.truncated);
        QJSValue ret = callback.call({engine->toScriptValue(result)});
        if(ret.isError())
            qWarning().noquote() << "kazamanager.query callback failed:" << ret.toString();
    });
}

void KaZaServiceManager::runQuery(const QString &sql, const QVariantMap &bindings, int timeoutMs, int maxRows,
                                  const Reply &reply)
{
    auto fail = [&](quint8 status, const QString &code, const QString &message) {
        reply(error(0, status, code, message));
    };
    if(!m_started)
    {
        fail(KaZaProtocol::ServiceUnavailable, QStringLiteral("database"), QStringLiteral("services unavailable"));
        return;
    }
    if(static_cast<int>(m_queue.size()) >= m_limits.queueLimit)
    {
        fail(KaZaProtocol::ServiceBusy, QStringLiteral("busy"), QStringLiteral("too many requests"));
        return;
    }

    ServiceJob job;
    QSet<QString> bindNames;
    QSet<QString> identNames;
    QString err;
    if(!KzService::analyzeStatement(sql, &job.sql, &bindNames, &identNames, &err) || !identNames.isEmpty())
    {
        qWarning().noquote() << "kazamanager.query refused:" << (err.isEmpty() ? QStringLiteral("%name% not allowed") : err);
        fail(KaZaProtocol::ServiceInvalidParams, QStringLiteral("sql"), QStringLiteral("invalid statement"));
        return;
    }
    if(QSet<QString>(bindings.keyBegin(), bindings.keyEnd()) != bindNames)
    {
        qWarning().noquote() << "kazamanager.query refused: bindings do not match the placeholders";
        fail(KaZaProtocol::ServiceInvalidParams, QStringLiteral("bindings"), QStringLiteral("bindings do not match"));
        return;
    }
    for(auto it = bindings.constBegin(); it != bindings.constEnd(); ++it)
    {
        switch(it.value().typeId())
        {
        case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
        case QMetaType::Double: case QMetaType::Float: case QMetaType::Bool: case QMetaType::QString:
        case QMetaType::QDateTime: case QMetaType::UnknownType: case QMetaType::Nullptr:
            break;
        default:
            qWarning().noquote() << "kazamanager.query refused: unsupported binding type for" << it.key();
            fail(KaZaProtocol::ServiceInvalidParams, QStringLiteral("bindings"), QStringLiteral("unsupported binding type"));
            return;
        }
    }
    job.bindings = bindings;
    job.timeoutMs = qBound(100, timeoutMs, KzService::HardTimeoutMs);
    job.maxRows = qBound(1, maxRows, KzService::HardMaxRows);
    job.key = addPending(nullptr, QStringLiteral("server"), 0, QStringLiteral("kazamanager.query"), reply, job.timeoutMs + 1000, true);
    m_queue.push_back(job);
    dispatch();
}

void KaZaServiceManager::dispatch()
{
    for(size_t i = 0; i < m_workers.size() && !m_queue.empty(); ++i)
    {
        if(m_workers[i].busy)
            continue;
        const ServiceJob job = m_queue.front();
        m_queue.pop_front();
        m_workers[i].busy = true;
        QMetaObject::invokeMethod(m_workers[i].worker, [w = m_workers[i].worker, job]() { w->execute(job); },
                                  Qt::QueuedConnection);
    }
}

void KaZaServiceManager::_workerFinished(int worker, quint64 key, const KaZaProtocol::ServiceResponse &response)
{
    m_workers[worker].busy = false;
    if(m_pending.contains(key))
        complete(key, response);        // otherwise cancelled / timed out / owner gone: result dropped
    dispatch();
}

void KaZaServiceManager::complete(quint64 key, KaZaProtocol::ServiceResponse response)
{
    auto it = m_pending.find(key);
    if(it == m_pending.end())
        return;
    Pending p = it.value();
    drop(key);

    response.id = p.id;
    qInfo().noquote() << "Service" << p.service << "user" << p.user << "status" << response.status
                      << "rows" << response.rows.size() << "in" << p.clock.elapsed() << "ms";
    if(p.internal || p.owner)
        p.reply(response);
}

void KaZaServiceManager::drop(quint64 key)
{
    auto it = m_pending.find(key);
    if(it == m_pending.end())
        return;
    if(it->owner)
    {
        auto o = m_byOwner.find(it->owner.data());
        if(o != m_byOwner.end())
            o->remove(it->id);
    }
    if(it->request)
        it->request->invalidate();
    delete it->timer;
    m_pending.erase(it);
}

void KaZaServiceManager::dropOwner(QObject *owner)
{
    const QHash<quint32, quint64> inflight = m_byOwner.take(owner);
    for(quint64 key : inflight)
    {
        for(auto q = m_queue.begin(); q != m_queue.end(); ++q)
        {
            if(q->key == key)
            {
                m_queue.erase(q);
                break;
            }
        }
        auto it = m_pending.find(key);
        if(it != m_pending.end())
        {
            if(it->request)
                it->request->invalidate();
            delete it->timer;
            m_pending.erase(it);
        }
    }
}

void KaZaServiceManager::cancel(QObject *owner, quint32 id)
{
    auto o = m_byOwner.find(owner);
    if(o == m_byOwner.end() || !o->contains(id))
        return;
    const quint64 key = o->value(id);
    for(auto q = m_queue.begin(); q != m_queue.end(); ++q)
    {
        if(q->key == key)
        {
            m_queue.erase(q);
            break;
        }
    }
    // A statement already running is not interrupted: it is bounded by statement_timeout and its result is dropped.
    drop(key);
}

QJsonArray KaZaServiceManager::describe(const QString &user) const
{
    QJsonArray array;
    QStringList names = registry().keys();
    names.sort();
    for(const QString &n : std::as_const(names))
    {
        KzService *svc = registry().value(n);
        if(svc && svc->allows(user))
            array.append(svc->describe());
    }
    return array;
}
