// Integration tests of the service engine against a throw-away PostgreSQL cluster
// (initdb/pg_ctl must be in PATH, otherwise the tests are skipped).
// Build: cmake -S tests/services -B <dir> && cmake --build <dir> && <dir>/tst_services
#include <QtTest>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <qqml.h>
#include "kazaservicemanager.h"
#include "kzservice.h"
#include "kazaelement.h"

using Response = KaZaProtocol::ServiceResponse;

// Stand-in for the `kazamanager` context property: forwards to the engine like KaZaManager::query does
class KzFacade : public QObject
{
    Q_OBJECT
public:
    KaZaServiceManager *manager {nullptr};
    QJSEngine *engine {nullptr};
    Q_INVOKABLE void query(const QString &sql, const QVariantMap &bindings, const QJSValue &callback)
    {
        manager->queryScript(sql, bindings, callback, engine);
    }
};

class TstServices : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    bool m_pgStarted {false};
    static constexpr int Port = 54329;
    QList<KzService *> m_services;
    KaZaServiceManager *m_manager {nullptr};
    QJSEngine m_js;
    KzFacade m_facade;

    KzService *makeScripted(const QString &name, const QString &code, const QList<KzParam *> &params = {})
    {
        auto *s = new KzService;
        s->setName(name);
        s->setScriptEngine(&m_js);
        s->setHandler(m_js.evaluate("(" + code + ")"));
        for (KzParam *p : params)
            s->addParam(p);
        m_services.append(s);
        return s;
    }

    ServiceDbConfig dbConfig() const
    {
        ServiceDbConfig c;
        c.driver = "QPSQL";
        c.dbName = "postgres";
        c.hostName = "127.0.0.1";
        c.port = Port;
        c.userName = "postgres";
        return c;
    }

    static bool run(const QString &program, const QStringList &args)
    {
        QProcess p;
        p.start(program, args);
        if (!p.waitForFinished(60000) || p.exitCode() != 0) {
            qWarning().noquote() << program << args << p.readAllStandardError() << p.readAllStandardOutput();
            return false;
        }
        return true;
    }

    KzService *makeService(const QString &name, const QString &sql, const QList<KzParam *> &params = {})
    {
        auto *s = new KzService;
        s->setName(name);
        s->setSql(sql);
        for (KzParam *p : params)
            s->addParam(p);
        m_services.append(s);
        return s;
    }

    static KzParam *param(const QString &name, KzParam::Type type, bool required = false)
    {
        auto *p = new KzParam;
        p->setName(name);
        p->setType(type);
        p->setRequired(required);
        return p;
    }

    bool compiles(KzService *s, QString *error = nullptr)
    {
        QString e;
        const bool ok = s->compile(&e);
        if (error)
            *error = e;
        return ok;
    }

    // Sends a request and waits for its reply (or the absence of reply when expectReply is false)
    Response call(QObject *owner, quint32 id, const QString &service, const QVariantMap &params = {},
                  const QString &user = "alice", bool *got = nullptr, int waitMs = 5000)
    {
        Response out;
        bool received = false;
        m_manager->request(owner, user, id, service, params, [&](const Response &r) { out = r; received = true; });
        QElapsedTimer t;
        t.start();
        while (!received && t.elapsed() < waitMs)
            QTest::qWait(10);
        if (got)
            *got = received;
        return out;
    }

    int countRows(const QString &table)
    {
        QSqlDatabase db = QSqlDatabase::database("test");
        QSqlQuery q(db);
        q.exec("SELECT count(*) FROM " + table);
        q.next();
        return q.value(0).toInt();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString data = m_dir.filePath("data");
        if (!run("initdb", {"-D", data, "-U", "postgres", "--auth=trust", "-E", "UTF8", "--locale=C"}))
            QSKIP("initdb not available");
        if (!run("pg_ctl", {"-D", data, "-o", QString("-p %1 -c listen_addresses=127.0.0.1 -c unix_socket_directories=''").arg(Port),
                            "-l", m_dir.filePath("pg.log"), "-w", "start"}))
            QSKIP("cannot start PostgreSQL");
        m_pgStarted = true;

        QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL", "test");
        db.setHostName("127.0.0.1");
        db.setPort(Port);
        db.setDatabaseName("postgres");
        QVERIFY2(db.open("postgres", ""), qPrintable(db.lastError().text()));
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE t (time timestamptz NOT NULL, temp double precision, name text)"));
        QVERIFY(q.exec("INSERT INTO t SELECT '2026-01-01T00:00:00Z'::timestamptz + (i || ' minutes')::interval, i / 10.0, 'n' || (i % 3) "
                       "FROM generate_series(0, 599) i"));

        qmlRegisterType<KzParam>("org.kazoe.kaza", 1, 0, "KzParam");
        qmlRegisterType<KzService>("org.kazoe.kaza", 1, 0, "KzService");
        qmlRegisterType<KaZaElement>("org.kazoe.kaza", 1, 0, "KaZaElement");
        m_manager = new KaZaServiceManager(this);
        m_facade.manager = m_manager;
        m_facade.engine = &m_js;
        m_js.globalObject().setProperty("kz", m_js.newQObject(&m_facade));
        KaZaServiceManager::Limits limits;
        limits.workers = 2;
        limits.maxInFlight = 2;
        limits.queueLimit = 16;
        m_manager->start(dbConfig(), limits);
    }

    void cleanup()
    {
        qDeleteAll(m_services);
        m_services.clear();
    }

    void cleanupTestCase()
    {
        delete m_manager;
        m_manager = nullptr;
        QSqlDatabase::database("test").close();
        QSqlDatabase::removeDatabase("test");
        if (m_pgStarted)
            run("pg_ctl", {"-D", m_dir.filePath("data"), "-m", "immediate", "stop"});
    }

    // ---- declaration checks --------------------------------------------------------------

    void declarationRejectsUnsafeSql_data()
    {
        QTest::addColumn<QString>("sql");
        QTest::newRow("two statements") << "SELECT 1; DROP TABLE t";
        QTest::newRow("comment") << "SELECT 1 -- x";
        QTest::newRow("block comment") << "SELECT 1 /* x */";
        QTest::newRow("dollar") << "SELECT $$x$$";
        QTest::newRow("question mark") << "SELECT ?";
        QTest::newRow("not a select") << "DELETE FROM t";
        QTest::newRow("update") << "UPDATE t SET temp = 1";
        QTest::newRow("placeholder without param") << "SELECT :nope";
        QTest::newRow("identifier without param") << "SELECT %col% FROM t";
        QTest::newRow("empty") << "  ;";
        QTest::newRow("unterminated quote") << "SELECT 'abc";
    }

    void declarationRejectsUnsafeSql()
    {
        QFETCH(QString, sql);
        KzService *s = makeService("bad", sql);
        QString error;
        QVERIFY2(!compiles(s, &error), qPrintable(sql));
        QVERIFY(!error.isEmpty());
        bool got = false;
        QObject owner;
        const Response r = call(&owner, 1, "bad", {}, "alice", &got);
        QVERIFY(got);
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceUnknown));
    }

    void declarationRejectsIdentifierWithoutClosedList()
    {
        KzService *s = makeService("bad", "SELECT %col% FROM t", {param("col", KzParam::Identifier)});
        QVERIFY(!compiles(s));
    }

    void declarationRejectsIdentifierInsideQuotedString()
    {
        auto *p = param("col", KzParam::Identifier);
        p->setValues({"temp"});
        KzService *s = makeService("bad", "SELECT %col%, '%col%' FROM t", {p});
        QVERIFY(!compiles(s));
    }

    void declarationRejectsBindingAnIdentifierParam()
    {
        auto *p = param("col", KzParam::Identifier);
        p->setValues({"temp"});
        KzService *s = makeService("bad", "SELECT :col FROM t", {p});
        QVERIFY(!compiles(s));
    }

    void declarationRejectsDuplicateService()
    {
        KzService *a = makeService("dup", "SELECT 1");
        KzService *b = makeService("dup", "SELECT 2");
        QVERIFY(compiles(a));
        QVERIFY(!compiles(b));
    }

    void castIsNotAPlaceholder()
    {
        KzService *s = makeService("cast", "SELECT 1::int AS one, :x::int AS x", {param("x", KzParam::Int, true)});
        QString error;
        QVERIFY2(compiles(s, &error), qPrintable(error));
    }

    // ---- execution -----------------------------------------------------------------------

    void historyHappyPath()
    {
        auto *metric = param("metric", KzParam::Identifier, true);
        metric->setValues({"temp"});
        auto *step = param("step", KzParam::Enum);
        step->setValues({"10 minutes", "1 hour"});
        step->setDefaultValue("1 hour");
        KzService *s = makeService("history",
            "SELECT date_bin(:step::interval, time, TIMESTAMPTZ '2026-01-01') AS t, min(%metric%), avg(%metric%), max(%metric%) "
            "FROM t WHERE time >= :start AND time < :end GROUP BY t ORDER BY t",
            {param("start", KzParam::DateTime, true), param("end", KzParam::DateTime, true), step, metric});
        QString error;
        QVERIFY2(compiles(s, &error), qPrintable(error));

        QObject owner;
        const QDateTime start(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const Response r = call(&owner, 7, "history",
                                {{"start", start}, {"end", start.addSecs(3 * 3600)}, {"metric", "temp"}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.id, 7u);
        QCOMPARE(r.kind, quint8(KaZaProtocol::ServiceKindTable));
        QCOMPARE(r.rows.size(), 3);      // default step: 1 hour
        QCOMPARE(r.columns.size(), 4);
        QCOMPARE(r.rows.at(0).at(1).toDouble(), 0.0);
        QCOMPARE(r.rows.at(0).at(3).toDouble(), 5.9);
        QVERIFY(!r.truncated);
    }

    void invalidParameters_data()
    {
        QTest::addColumn<QVariantMap>("params");
        QTest::addColumn<QString>("badParam");
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QTest::newRow("missing required") << QVariantMap{} << "n";
        QTest::newRow("unknown param") << QVariantMap{{"n", 1}, {"evil", 1}} << "evil";
        QTest::newRow("wrong type string") << QVariantMap{{"n", "1"}} << "n";
        QTest::newRow("wrong type datetime") << QVariantMap{{"n", now}} << "n";
        QTest::newRow("below min") << QVariantMap{{"n", -1}} << "n";
        QTest::newRow("above max") << QVariantMap{{"n", 101}} << "n";
        QTest::newRow("double for int") << QVariantMap{{"n", 1.5}} << "n";
        QTest::newRow("enum not allowed") << QVariantMap{{"n", 1}, {"mode", "x; DROP TABLE t"}} << "mode";
        QTest::newRow("string too long") << QVariantMap{{"n", 1}, {"label", QString(300, 'a')}} << "label";
    }

    void invalidParameters()
    {
        QFETCH(QVariantMap, params);
        QFETCH(QString, badParam);
        auto *n = param("n", KzParam::Int, true);
        n->setMin(0);
        n->setMax(100);
        auto *mode = param("mode", KzParam::Enum);
        mode->setValues({"a", "b"});
        KzService *s = makeService("check", "SELECT :n::int, :mode::text, :label::text",
                                   {n, mode, param("label", KzParam::String)});
        QVERIFY(compiles(s));
        QObject owner;
        const Response r = call(&owner, 1, "check", params);
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceInvalidParams));
        QCOMPARE(r.errorCode, badParam);
        QVERIFY(!r.errorMessage.contains("SELECT"));
    }

    void injectionIsBoundNotConcatenated()
    {
        KzService *s = makeService("byname", "SELECT count(*) FROM t WHERE name = :name", {param("name", KzParam::String, true)});
        s->setSingle(true);
        QVERIFY(compiles(s));
        QObject owner;
        Response r = call(&owner, 1, "byname", {{"name", "n1"}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.kind, quint8(KaZaProtocol::ServiceKindValue));
        QCOMPARE(r.value.toInt(), 200);

        r = call(&owner, 2, "byname", {{"name", "' OR '1'='1"}});
        QCOMPARE(r.value.toInt(), 0);
        r = call(&owner, 3, "byname", {{"name", "x'; DROP TABLE t; --"}});
        QCOMPARE(r.value.toInt(), 0);
        QCOMPARE(countRows("t"), 600);
    }

    void unknownServiceAndAclAreIndistinguishable()
    {
        KzService *s = makeService("private", "SELECT 1");
        s->setUsers({"bob"});
        QVERIFY(compiles(s));
        QObject owner;
        const Response denied = call(&owner, 1, "private", {}, "alice");
        const Response unknown = call(&owner, 2, "nothing", {}, "alice");
        QCOMPARE(denied.status, quint8(KaZaProtocol::ServiceUnknown));
        QCOMPARE(denied.status, unknown.status);
        QCOMPARE(denied.errorCode, unknown.errorCode);
        QCOMPARE(denied.errorMessage, unknown.errorMessage);
        QCOMPARE(call(&owner, 3, "private", {}, "bob").status, quint8(KaZaProtocol::ServiceOk));
    }

    void maxRowsTruncates()
    {
        KzService *s = makeService("all", "SELECT time FROM t ORDER BY time");
        s->setMaxRows(25);
        QVERIFY(compiles(s));
        QObject owner;
        const Response r = call(&owner, 1, "all");
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.rows.size(), 25);
        QVERIFY(r.truncated);
    }

    void statementTimeout()
    {
        KzService *s = makeService("sleep", "SELECT pg_sleep(:s)", {param("s", KzParam::Double, true)});
        s->setTimeout(300);
        QVERIFY(compiles(s));
        QObject owner;
        QElapsedTimer t;
        t.start();
        const Response r = call(&owner, 1, "sleep", {{"s", 5.0}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceTimeout));
        QVERIFY2(t.elapsed() < 2500, "timed out too late");
        // the worker is usable again afterwards
        s->setTimeout(10000);
        QCOMPARE(call(&owner, 2, "sleep", {{"s", 0.0}}).status, quint8(KaZaProtocol::ServiceOk));
    }

    void writesAreRefusedByTheDatabase()
    {
        // Accepted by the declaration check (starts with WITH) but must fail in the READ ONLY transaction
        KzService *s = makeService("sneaky", "WITH x AS (INSERT INTO t VALUES (now(), 1, 'x') RETURNING 1) SELECT * FROM x");
        QVERIFY(compiles(s));
        QObject owner;
        const Response r = call(&owner, 1, "sneaky");
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceInternal));
        QVERIFY(!r.errorMessage.contains("read-only"));
        QCOMPARE(countRows("t"), 600);
    }

    void failedStatementDoesNotPoisonTheConnection()
    {
        KzService *bad = makeService("broken", "SELECT * FROM does_not_exist");
        KzService *good = makeService("fine", "SELECT 42");
        good->setSingle(true);
        QVERIFY(compiles(bad));
        QVERIFY(compiles(good));
        QObject owner;
        for (quint32 i = 0; i < 4; ++i) {
            const Response r = call(&owner, i * 2, "broken");
            QCOMPARE(r.status, quint8(KaZaProtocol::ServiceInternal));
            QVERIFY(!r.errorMessage.contains("does_not_exist"));
            const Response ok = call(&owner, i * 2 + 1, "fine");
            QCOMPARE(ok.status, quint8(KaZaProtocol::ServiceOk));
            QCOMPARE(ok.value.toInt(), 42);
        }
    }

    void tooManyInFlightIsBusy()
    {
        KzService *s = makeService("slow", "SELECT pg_sleep(0.6)");
        QVERIFY(compiles(s));
        QObject owner;
        QList<Response> replies;
        auto cb = [&](const Response &r) { replies.append(r); };
        m_manager->request(&owner, "alice", 1, "slow", {}, cb);
        m_manager->request(&owner, "alice", 2, "slow", {}, cb);
        m_manager->request(&owner, "alice", 3, "slow", {}, cb);   // limit is 2 per owner
        QTRY_VERIFY(!replies.isEmpty());
        QCOMPARE(replies.at(0).id, 3u);
        QCOMPARE(replies.at(0).status, quint8(KaZaProtocol::ServiceBusy));
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 3, 5000);
    }

    void duplicateIdIsRejected()
    {
        KzService *s = makeService("slow", "SELECT pg_sleep(0.4)");
        QVERIFY(compiles(s));
        QObject owner;
        QList<Response> replies;
        auto cb = [&](const Response &r) { replies.append(r); };
        m_manager->request(&owner, "alice", 5, "slow", {}, cb);
        m_manager->request(&owner, "alice", 5, "slow", {}, cb);
        QTRY_VERIFY(!replies.isEmpty());
        QCOMPARE(replies.at(0).status, quint8(KaZaProtocol::ServiceInvalidParams));
        QCOMPARE(replies.at(0).errorCode, QStringLiteral("id"));
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 2, 5000);
    }

    void cancelledRequestGetsNoReply()
    {
        KzService *s = makeService("slow", "SELECT pg_sleep(0.5)");
        QVERIFY(compiles(s));
        QObject owner;
        int replies = 0;
        auto cb = [&](const Response &) { ++replies; };
        m_manager->request(&owner, "alice", 1, "slow", {}, cb);
        m_manager->request(&owner, "alice", 2, "slow", {}, cb);
        m_manager->cancel(&owner, 2);
        QTest::qWait(1500);
        QCOMPARE(replies, 1);   // request 1 answered, request 2 cancelled
        QCOMPARE(m_manager->pendingCount(), 0);
        // cancelling an unknown id is harmless
        m_manager->cancel(&owner, 99);
    }

    void destroyedOwnerGetsNoReplyAndNoCrash()
    {
        KzService *s = makeService("slow", "SELECT pg_sleep(0.4)");
        QVERIFY(compiles(s));
        int replies = 0;
        {
            auto *owner = new QObject;
            m_manager->request(owner, "alice", 1, "slow", {}, [&](const Response &) { ++replies; });
            delete owner;
        }
        QTest::qWait(1200);
        QCOMPARE(replies, 0);
        QCOMPARE(m_manager->pendingCount(), 0);
    }

    void describeListsOnlyVisibleServices()
    {
        auto *p = param("n", KzParam::Int, true);
        p->setMin(0);
        p->setMax(5);
        KzService *pub = makeService("pub", "SELECT :n::int", {p});
        pub->setDescription("public one");
        KzService *priv = makeService("priv", "SELECT 1");
        priv->setUsers({"bob"});
        QVERIFY(compiles(pub));
        QVERIFY(compiles(priv));
        const QJsonArray forAlice = m_manager->describe("alice");
        QCOMPARE(forAlice.size(), 1);
        const QJsonObject o = forAlice.at(0).toObject();
        QCOMPARE(o.value("name").toString(), QStringLiteral("pub"));
        QCOMPARE(o.value("params").toArray().at(0).toObject().value("type").toString(), QStringLiteral("int"));
        QCOMPARE(m_manager->describe("bob").size(), 2);
    }

    // ---- scriptable services ---------------------------------------------------------------

    void scriptedTableAndParams()
    {
        auto *n = param("n", KzParam::Int, true);
        n->setMin(0);
        n->setMax(10);
        KzService *s = makeScripted("script.table",
            "function(req) { req.respondTable([\"a\", \"b\"], [[req.params.n, \"x\"], [req.params.n * 2, \"y\"]]) }", {n});
        QString error;
        QVERIFY2(compiles(s, &error), qPrintable(error));
        QObject owner;
        const Response r = call(&owner, 1, "script.table", {{"n", 4}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.kind, quint8(KaZaProtocol::ServiceKindTable));
        QCOMPARE(r.columns, QStringList({"a", "b"}));
        QCOMPARE(r.rows.size(), 2);
        QCOMPARE(r.rows.at(1).at(0).toInt(), 8);
        QCOMPARE(r.rows.at(1).at(1).toString(), QStringLiteral("y"));
        // validation happens before the handler runs
        QCOMPARE(call(&owner, 2, "script.table", {{"n", 99}}).status, quint8(KaZaProtocol::ServiceInvalidParams));
        QCOMPARE(call(&owner, 3, "script.table", {}).errorCode, QStringLiteral("n"));
    }

    void scriptedAnswersAfterAnAsynchronousQuery()
    {
        KzService *s = makeScripted("script.count",
            "function(req) { kz.query(\"SELECT count(*) AS c FROM t WHERE name = :n\", {n: req.params.name}, "
            "function(r) { if (!r.ok) req.fail(\"db\"); else req.respondValue(r.rows[0][0]) }) }",
            {param("name", KzParam::String, true)});
        QVERIFY(compiles(s));
        QObject owner;
        const Response r = call(&owner, 1, "script.count", {{"name", "n2"}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.kind, quint8(KaZaProtocol::ServiceKindValue));
        QCOMPARE(r.value.toInt(), 200);
    }

    void scriptedExceptionAnswersInternalAtOnce()
    {
        KzService *s = makeScripted("script.throw", "function(req) { throw new Error(\"secret boom\") }");
        QVERIFY(compiles(s));
        QObject owner;
        QElapsedTimer t;
        t.start();
        const Response r = call(&owner, 1, "script.throw");
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceInternal));
        QVERIFY(!r.errorMessage.contains("boom"));
        QVERIFY2(t.elapsed() < 500, "should not wait for the timeout");
    }

    void scriptedWithoutAnswerTimesOutAndLateAnswerIsIgnored()
    {
        KzService *s = makeScripted("script.silent", "function(req) { lastReq = req }");
        s->setTimeout(300);
        QVERIFY(compiles(s));
        QObject owner;
        const Response r = call(&owner, 1, "script.silent");
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceTimeout));
        QVERIFY(m_js.evaluate("lastReq.finished").toBool());
        m_js.evaluate("lastReq.respondValue(1)");     // too late: no effect, no crash
        QCOMPARE(m_manager->pendingCount(), 0);
    }

    void scriptedFailInvalidAndSanitizedCodes()
    {
        KzService *s = makeScripted("script.errors",
            "function(req) { switch (req.params.kind) {"
            " case \"fail\": req.fail(\"not_found\"); break;"
            " case \"badcode\": req.fail(\"SELECT * FROM t\"); break;"
            " case \"invalid\": req.invalid(\"kind\", \"nope\"); break; } }",
            {param("kind", KzParam::String, true)});
        QVERIFY(compiles(s));
        QObject owner;
        Response r = call(&owner, 1, "script.errors", {{"kind", "fail"}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceInternal));
        QCOMPARE(r.errorCode, QStringLiteral("not_found"));
        r = call(&owner, 2, "script.errors", {{"kind", "badcode"}});
        QCOMPARE(r.errorCode, QStringLiteral("internal"));
        r = call(&owner, 3, "script.errors", {{"kind", "invalid"}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceInvalidParams));
        QCOMPARE(r.errorCode, QStringLiteral("kind"));
    }

    void scriptedOnlyFirstAnswerCounts()
    {
        KzService *s = makeScripted("script.twice", "function(req) { req.respondValue(1); req.respondValue(2); req.fail(\"x\") }");
        QVERIFY(compiles(s));
        QObject owner;
        int replies = 0;
        Response first;
        m_manager->request(&owner, "alice", 1, "script.twice", {}, [&](const Response &r) { ++replies; first = r; });
        QTest::qWait(200);
        QCOMPARE(replies, 1);
        QCOMPARE(first.value.toInt(), 1);
    }

    void scriptedRowsAreCappedByMaxRows()
    {
        KzService *s = makeScripted("script.many",
            "function(req) { var rows = []; for (var i = 0; i < 50; i++) rows.push([i]); req.respondTable([\"i\"], rows) }");
        s->setMaxRows(10);
        QVERIFY(compiles(s));
        QObject owner;
        const Response r = call(&owner, 1, "script.many");
        QCOMPARE(r.rows.size(), 10);
        QVERIFY(r.truncated);
    }

    void scriptedDeclarationRules()
    {
        // both sql and handler
        KzService *both = makeScripted("script.both", "function(req) {}");
        both->setSql("SELECT 1");
        QVERIFY(!compiles(both));
        // neither
        KzService *none = makeService("script.none", "");
        QVERIFY(!compiles(none));
        // handler without engine
        auto *noEngine = new KzService;
        m_services.append(noEngine);
        noEngine->setName("script.noengine");
        noEngine->setHandler(m_js.evaluate("(function(req) {})"));
        QVERIFY(!compiles(noEngine));
        // identifier parameters make no sense without sql
        auto *id = param("col", KzParam::Identifier);
        id->setValues({"temp"});
        KzService *ident = makeScripted("script.ident", "function(req) {}", {id});
        QVERIFY(!compiles(ident));
    }

    void queryScriptRefusesUnsafeStatements()
    {
        auto check = [&](const QString &sql, const QString &bindings, const QString &expectedCode) {
            m_js.evaluate(QString("var res = null; kz.query(\"%1\", %2, function(r) { res = r })").arg(sql, bindings));
            QTRY_VERIFY2(!m_js.evaluate("res").isNull(), qPrintable(sql));
            QVERIFY2(!m_js.evaluate("res.ok").toBool(), qPrintable(sql));
            QCOMPARE(m_js.evaluate("res.errorCode").toString(), expectedCode);
        };
        check("DELETE FROM t", "{}", "sql");
        check("SELECT 1; SELECT 2", "{}", "sql");
        check("SELECT :a", "{}", "bindings");
        check("SELECT :a", "{a: 1, b: 2}", "bindings");
        check("SELECT %col% FROM t", "{}", "sql");
        check("SELECT :a", "{a: {x: 1}}", "bindings");
        // writable CTE reaches the database, which refuses it
        m_js.evaluate("var res = null; kz.query(\"WITH x AS (INSERT INTO t VALUES (now(), 1, 'x') RETURNING 1) SELECT * FROM x\", {}, function(r) { res = r })");
        QTRY_VERIFY(!m_js.evaluate("res").isNull());
        QVERIFY(!m_js.evaluate("res.ok").toBool());
        QCOMPARE(countRows("t"), 600);
    }

    void queryScriptReturnsRows()
    {
        m_js.evaluate("var res = null; kz.query(\"SELECT time, temp FROM t WHERE temp < :max ORDER BY time\", {max: 0.35}, function(r) { res = r })");
        QTRY_VERIFY(!m_js.evaluate("res").isNull());
        QVERIFY(m_js.evaluate("res.ok").toBool());
        QCOMPARE(m_js.evaluate("res.rows.length").toInt(), 4);
        QCOMPARE(m_js.evaluate("res.columns.join()").toString(), QStringLiteral("time,temp"));
        QVERIFY(m_js.evaluate("res.rows[0][0] instanceof Date").toBool());
    }

    // ---- loading from QML ------------------------------------------------------------------

    void declaredInQml()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml 2.0
            import org.kazoe.kaza 1.0
            QtObject {
                property list<QtObject> declared: [
                KzService {
                    name: "qml.history"
                    description: "from qml"
                    sql: "SELECT count(*) AS c FROM t WHERE time >= :start AND temp < :max AND name = :step"
                    params: [
                        KzParam { name: "start"; type: KzParam.DateTime; required: true },
                        KzParam { name: "max"; type: KzParam.Double; defaultValue: 5; min: 0; max: 100 },
                        KzParam { name: "step"; type: KzParam.Enum; values: ["n0", "n1"]; defaultValue: "n1" }
                    ]
                    maxRows: 10
                },
                KzService {
                    name: "qml.handler"
                    handler: function(req) { req.respondValue(req.params.k + 1) }
                    params: [ KzParam { name: "k"; type: KzParam.Int; defaultValue: 41 } ]
                },
                KzService {
                    name: "qml.bad"
                    sql: "SELECT :unknown"
                }
                ]
            }
        )", QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        const QJsonArray list = m_manager->describe("alice");
        QStringList names;
        for (const QJsonValue &v : list)
            names << v.toObject().value("name").toString();
        QVERIFY(names.contains("qml.history"));
        QVERIFY(names.contains("qml.handler"));
        QVERIFY2(!names.contains("qml.bad"), "an invalid declaration must not be registered");

        QObject owner;
        const QDateTime start(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        Response r = call(&owner, 1, "qml.history", {{"start", start}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.rows.size(), 1);
        r = call(&owner, 2, "qml.handler");
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.value.toInt(), 42);
        QCOMPARE(call(&owner, 3, "qml.bad").status, quint8(KaZaProtocol::ServiceUnknown));
        root.reset();
        QVERIFY(m_manager->describe("alice").isEmpty());   // services unregister when destroyed
    }

    // Declarations of the real application (KaZaTrespeyres/Server/EcoWittServices.qml), run against tables
    // with the same columns. Set KAZA_ECOWITT_SERVICES to the path of the file to enable this test.
    void ecowittServicesOfTheApplication()
    {
        const QString path = qEnvironmentVariable("KAZA_ECOWITT_SERVICES");
        if (path.isEmpty())
            QSKIP("KAZA_ECOWITT_SERVICES not set");
        QSqlQuery q(QSqlDatabase::database("test"));
        QVERIFY(q.exec("CREATE TABLE ecowitt (time timestamptz PRIMARY KEY, tempin float8, humidityin float8, baromrel float8, baromabs float8,"
                       " temp float8, humidity float8, vpd float8, winddir float8, windspeed float8, windgust float8, solarradiation float8,"
                       " uv float8, rrain_piezo float8, soilmoisture1 float8, soilmoisture2 float8)"));
        QVERIFY(q.exec("INSERT INTO ecowitt (time, temp, humidity) SELECT '2026-02-01T00:00:00Z'::timestamptz + (i || ' minutes')::interval, i / 10.0, 50 "
                       "FROM generate_series(0, 359, 15) i"));
        QVERIFY(q.exec("CREATE TABLE ecowitt_daily (day date PRIMARY KEY, temp_min float8, temp_max float8, temp_avg float8, humidity_min float8,"
                       " humidity_max float8, humidity_avg float8, windspeed_avg float8, windgust_max float8, solar_max float8, rain_total float8, samples int)"));
        QVERIFY(q.exec("INSERT INTO ecowitt_daily (day, temp_min, temp_max, temp_avg, rain_total, samples) VALUES ('2026-02-01', 1, 9, 5, 2.5, 96), ('2026-02-02', 2, 8, 4, 0, 96)"));

        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(path));
        QVERIFY2(component.status() != QQmlComponent::Null, "component not loaded");
        QScopedPointer<QObject> root(component.create());
        if (!root)
            QSKIP(qPrintable("cannot instantiate (KaZaElement not available here): " + component.errorString()));
        QObject owner;
        const QDateTime start(QDate(2026, 2, 1), QTime(0, 0), QTimeZone::UTC);
        Response r = call(&owner, 1, "ecowitt.history", {{"start", start}, {"end", start.addDays(1)}, {"metric", "temp"}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.columns, QStringList({"time", "min", "avg", "max"}));
        QCOMPARE(r.rows.size(), 6);                                 // 6 hours of data, 1 hour step
        QCOMPARE(r.rows.at(0).at(1).toDouble(), 0.0);
        QVERIFY(r.rows.at(0).at(0).toDateTime().isValid());
        r = call(&owner, 2, "ecowitt.history", {{"start", start}, {"end", start.addDays(1)}, {"metric", "temp; DROP TABLE ecowitt"}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceInvalidParams));
        QCOMPARE(r.errorCode, QStringLiteral("metric"));
        r = call(&owner, 3, "ecowitt.history", {{"start", start}, {"end", start.addDays(1)}});
        QCOMPARE(r.errorCode, QStringLiteral("metric"));
        r = call(&owner, 4, "ecowitt.daily", {{"start", start}, {"end", start.addDays(2)}});
        QCOMPARE(r.status, quint8(KaZaProtocol::ServiceOk));
        QCOMPARE(r.rows.size(), 2);
        QCOMPARE(r.columns.first(), QStringLiteral("day"));
        QCOMPARE(r.rows.at(0).at(r.columns.indexOf("rain_total")).toDouble(), 2.5);
        QSqlQuery cleanup(QSqlDatabase::database("test"));
        cleanup.exec("DROP TABLE ecowitt");
        cleanup.exec("DROP TABLE ecowitt_daily");
    }

    void withoutDatabaseEverythingIsUnavailable()
    {
        KaZaServiceManager idle;
        Response out;
        bool got = false;
        QObject owner;
        idle.request(&owner, "alice", 1, "whatever", {}, [&](const Response &r) { out = r; got = true; });
        QVERIFY(got);
        QCOMPARE(out.status, quint8(KaZaProtocol::ServiceUnavailable));
    }
};

QTEST_MAIN(TstServices)
#include "tst_services.moc"
