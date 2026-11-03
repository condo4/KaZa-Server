#ifndef KZSERVICE_H
#define KZSERVICE_H

#include <QObject>
#include <QJsonObject>
#include <QJSEngine>
#include <QJSValue>
#include <QQmlListProperty>
#include <QQmlParserStatus>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include "kzparam.h"

// A named, parameterised, read-only SQL service exposed to clients (see doc/08-services.md).
// Declared in the server QML:
//
//   KzService {
//       name: "ecowitt.history"
//       sql: "SELECT time_bucket(:step::interval, time) AS t, avg(temp) FROM ecowitt
//             WHERE time >= :start AND time < :end GROUP BY t ORDER BY t"
//       params: [ KzParam { name: "start"; type: KzParam.DateTime; required: true }, ... ]
//   }
//
// Clients never see the statement. `:name` placeholders are bound, `%name%` placeholders (KzParam.Identifier
// only) are replaced by a value taken from a closed list.
class KzService : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed FINAL)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY changed FINAL)
    Q_PROPERTY(QString sql READ sql WRITE setSql NOTIFY changed FINAL)
    Q_PROPERTY(QQmlListProperty<KzParam> params READ params FINAL)
    Q_PROPERTY(int maxRows READ maxRows WRITE setMaxRows NOTIFY changed FINAL)
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY changed FINAL)
    Q_PROPERTY(QStringList users READ users WRITE setUsers NOTIFY changed FINAL)
    Q_PROPERTY(bool single READ single WRITE setSingle NOTIFY changed FINAL)
    Q_PROPERTY(QJSValue handler READ handler WRITE setHandler NOTIFY changed FINAL)
    Q_CLASSINFO("DefaultProperty", "params")

public:
    static constexpr int DefaultMaxRows = 1000;
    static constexpr int HardMaxRows = 100000;
    static constexpr int DefaultTimeoutMs = 10000;
    static constexpr int HardTimeoutMs = 60000;

    explicit KzService(QObject *parent = nullptr);
    ~KzService() override;

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; emit changed(); }
    QString description() const { return m_description; }
    void setDescription(const QString &description) { m_description = description; emit changed(); }
    QString sql() const { return m_sql; }
    void setSql(const QString &sql) { m_sql = sql; emit changed(); }
    int maxRows() const { return qBound(1, m_maxRows, HardMaxRows); }
    void setMaxRows(int rows) { m_maxRows = rows; emit changed(); }
    int timeout() const { return qBound(100, m_timeout, HardTimeoutMs); }
    void setTimeout(int ms) { m_timeout = ms; emit changed(); }
    QStringList users() const { return m_users; }
    void setUsers(const QStringList &users) { m_users = users; emit changed(); }
    bool single() const { return m_single; }
    void setSingle(bool single) { m_single = single; emit changed(); }

    // Scriptable service: function(req) called on the main thread with the validated parameters
    // (instead of `sql`). It must answer through req.respond*() / req.fail(), possibly later.
    QJSValue handler() const { return m_handler; }
    void setHandler(const QJSValue &handler) { m_handler = handler; emit changed(); }
    bool isScripted() const { return m_handler.isCallable(); }
    // Engine that runs the handler: taken from the QML context, or set explicitly (tests)
    QJSEngine *scriptEngine() const { return m_scriptEngine; }
    void setScriptEngine(QJSEngine *engine) { m_scriptEngine = engine; }

    QQmlListProperty<KzParam> params();
    void addParam(KzParam *param);
    const QList<KzParam *> &paramList() const { return m_params; }

    // QQmlParserStatus
    void classBegin() override {}
    void componentComplete() override;

    // Validates the declaration (placeholders, closed lists, single read-only statement) and
    // registers the service. Returns false and fills `error` if the declaration is unsafe.
    bool compile(QString *error);
    bool isCompiled() const { return m_compiled; }

    bool allows(const QString &user) const;

    // Checks that `sql` is one SELECT/WITH statement without comments, `$` or `?`, and extracts its
    // placeholders: `bindNames` for ':name', `identNames` for '%name%'. `statement` is the trimmed text.
    static bool analyzeStatement(const QString &sql, QString *statement, QSet<QString> *bindNames,
                                 QSet<QString> *identNames, QString *error);

    // Validates client parameters against the declarations. `out` receives one entry per declared
    // parameter (null QVariant = SQL NULL).
    bool validateParams(const QVariantMap &clientParams, QVariantMap *out,
                        QString *errorParam, QString *errorMessage) const;

    // Validates client parameters against the declaration and builds the statement to execute:
    // `sql` has its identifiers substituted, `bindings` holds the values of the `:name` placeholders.
    // On failure `errorParam` names the offending parameter (empty when not parameter specific).
    bool buildStatement(const QVariantMap &clientParams, QString *sql, QVariantMap *bindings,
                        QString *errorParam, QString *errorMessage) const;

    QJsonObject describe() const;

signals:
    void changed();

private:
    static qsizetype paramCount(QQmlListProperty<KzParam> *list);
    static KzParam *paramAt(QQmlListProperty<KzParam> *list, qsizetype index);
    static void paramAppend(QQmlListProperty<KzParam> *list, KzParam *param);
    static void paramClear(QQmlListProperty<KzParam> *list);

    const KzParam *findParam(const QString &name) const;

    QString m_name;
    QString m_description;
    QString m_sql;
    QList<KzParam *> m_params;
    int m_maxRows {DefaultMaxRows};
    int m_timeout {DefaultTimeoutMs};
    QStringList m_users {QStringLiteral("*")};
    bool m_single {false};
    QJSValue m_handler;
    QJSEngine *m_scriptEngine {nullptr};

    bool m_compiled {false};
    QString m_statement;          // sql, trimmed, without trailing ';'
    QSet<QString> m_bindNames;    // :name placeholders found in the statement
    QSet<QString> m_identNames;   // %name% placeholders found in the statement
};

#endif // KZSERVICE_H
