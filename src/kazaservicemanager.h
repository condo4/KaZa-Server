#ifndef KAZASERVICEMANAGER_H
#define KAZASERVICEMANAGER_H

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJSValue>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <deque>
#include <functional>
#include <kazaprotocol.h>

class QJSEngine;
class KzService;
class KzServiceRequest;

struct ServiceDbConfig {
    QString driver;
    QString dbName;
    QString hostName;
    int port {0};
    QString userName;
    QString password;
};

// One statement to run on a worker connection. Built by KzService::buildStatement(), so `sql` only
// contains whitelisted identifiers and `bindings` the validated values of its ':name' placeholders.
struct ServiceJob {
    quint64 key {0};
    QString sql;
    QVariantMap bindings;
    int timeoutMs {0};
    int maxRows {0};
    bool single {false};
};
Q_DECLARE_METATYPE(ServiceJob)

// Runs in its own thread with its own database connection. Every statement runs inside a
// "BEGIN READ ONLY" transaction (always rolled back) with a statement_timeout, so that PostgreSQL
// itself refuses any write even if a service declaration were wrong.
class KaZaServiceWorker : public QObject
{
    Q_OBJECT
public:
    KaZaServiceWorker(const ServiceDbConfig &config, int index, QObject *parent = nullptr);

public slots:
    void execute(const ServiceJob &job);
    void shutdown();

signals:
    void finished(int worker, quint64 key, const KaZaProtocol::ServiceResponse &response);

private:
    bool ensureOpen();

    ServiceDbConfig m_config;
    int m_index;
    QString m_connectionName;
};

// Registry of KzService and execution engine of FRAME_REQUEST (see doc/08-services.md).
class KaZaServiceManager : public QObject
{
    Q_OBJECT
public:
    using Reply = std::function<void(const KaZaProtocol::ServiceResponse &)>;

    struct Limits {
        int workers {2};
        int maxInFlight {8};     // per owner (connection)
        int queueLimit {256};    // waiting jobs, all owners
    };

    explicit KaZaServiceManager(QObject *parent = nullptr);
    ~KaZaServiceManager() override;

    static KaZaServiceManager *instance();
    static bool registerService(KzService *service, QString *error);
    static void unregisterService(KzService *service);

    // Opens the worker pool. Without it every request is answered ServiceUnavailable.
    void start(const ServiceDbConfig &config, const Limits &limits);

    // `owner` identifies the connection: its requests are cancelled when it is destroyed.
    // `reply` is called exactly once unless the request is cancelled or the owner destroyed.
    void request(QObject *owner, const QString &user, quint32 id, const QString &service,
                 const QVariantMap &params, const Reply &reply);
    void cancel(QObject *owner, quint32 id);

    // Services visible to `user`
    QJsonArray describe(const QString &user) const;

    int pendingCount() const { return m_pending.size(); }

    // Answer of a scriptable service (called by KzServiceRequest)
    void respondScripted(quint64 key, const KaZaProtocol::ServiceResponse &response);

    // Read-only query for server QML code (kazamanager.query): same rules as a declarative service
    // (one SELECT/WITH statement, ':name' placeholders bound from `bindings`, READ ONLY transaction,
    // statement_timeout) executed on the worker pool. `reply` is always called, from the main thread.
    // Script flavour of runQuery: `callback(result)` gets { ok, status, errorCode, columns, rows, truncated }
    void queryScript(const QString &sql, const QVariantMap &bindings, const QJSValue &callback, QJSEngine *engine);

    void runQuery(const QString &sql, const QVariantMap &bindings, int timeoutMs, int maxRows, const Reply &reply);

private slots:
    void _workerFinished(int worker, quint64 key, const KaZaProtocol::ServiceResponse &response);

private:
    struct Worker {
        QThread *thread {nullptr};
        KaZaServiceWorker *worker {nullptr};
        bool busy {false};
    };
    struct Pending {
        QPointer<QObject> owner;
        QString service;
        QString user;
        quint32 id {0};
        Reply reply;
        bool internal {false};              // kazamanager.query: no owner, always answered
        QPointer<KzServiceRequest> request; // scriptable service
        QTimer *timer {nullptr};
        QElapsedTimer clock;
    };

    void dispatch();
    quint64 addPending(QObject *owner, const QString &user, quint32 id, const QString &service, const Reply &reply,
                       int timeoutMs, bool internal);
    void complete(quint64 key, KaZaProtocol::ServiceResponse response);
    void drop(quint64 key);
    void dropOwner(QObject *owner);
    static KaZaProtocol::ServiceResponse error(quint32 id, quint8 status, const QString &code, const QString &message);

    static KaZaServiceManager *s_instance;

    std::vector<Worker> m_workers;
    Limits m_limits;
    bool m_started {false};
    quint64 m_nextKey {1};
    std::deque<ServiceJob> m_queue;
    QHash<quint64, Pending> m_pending;
    QHash<QObject *, QHash<quint32, quint64>> m_byOwner;
};

#endif // KAZASERVICEMANAGER_H
