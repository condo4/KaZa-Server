#ifndef KZSERVICEREQUEST_H
#define KZSERVICEREQUEST_H

#include <QObject>
#include <QPointer>
#include <QVariantMap>

class KaZaServiceManager;

// Handed to the `handler` of a scriptable KzService. Parameters are already validated against the
// KzParam declarations. The handler runs on the main thread and must not block: for database access
// use kazamanager.query(sql, bindings, callback), then answer from the callback.
// Only the first respond*/fail/invalid call counts, later ones are ignored.
class KzServiceRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap params READ params CONSTANT FINAL)
    Q_PROPERTY(QString user READ user CONSTANT FINAL)
    Q_PROPERTY(QString service READ service CONSTANT FINAL)
    Q_PROPERTY(bool finished READ finished NOTIFY finishedChanged FINAL)

public:
    KzServiceRequest(KaZaServiceManager *manager, quint64 key, const QString &service, const QString &user,
                     const QVariantMap &params, int maxRows);

    QVariantMap params() const { return m_params; }
    QString user() const { return m_user; }
    QString service() const { return m_service; }
    bool finished() const { return m_finished; }

    // The request no longer expects an answer (cancelled, timed out, connection gone)
    void invalidate();

public slots:
    void respondTable(const QStringList &columns, const QVariantList &rows);
    void respondValue(const QVariant &value);
    void respondEmpty();
    // Error with an application specific code ([a-z0-9_]) ; the message is generic on the wire
    void fail(const QString &code);
    // INVALID_PARAMS for a parameter the declaration cannot check (cross-field rules)
    void invalid(const QString &param, const QString &message);

signals:
    void finishedChanged();

private:
    void finish(quint8 status, const QString &code, const QString &message, quint8 kind = 0,
                const QStringList &columns = {}, const QList<QList<QVariant>> &rows = {},
                const QVariant &value = QVariant(), bool truncated = false);

    QPointer<KaZaServiceManager> m_manager;
    quint64 m_key;
    QString m_service;
    QString m_user;
    QVariantMap m_params;
    int m_maxRows;
    bool m_finished {false};
};

#endif // KZSERVICEREQUEST_H
