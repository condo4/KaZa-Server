#ifndef KZALARM_H
#define KZALARM_H

#include <QObject>

class KzAlarm : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enable READ enable WRITE setEnable NOTIFY enableChanged FINAL)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged FINAL)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged FINAL)
    Q_PROPERTY(bool admin READ admin WRITE setAdmin NOTIFY adminChanged FINAL)
    Q_PROPERTY(bool debuge READ debug WRITE setDebug NOTIFY debugChanged FINAL)

public:
    explicit KzAlarm(QObject *parent = nullptr);

    bool enable() const;
    void setEnable(bool newEnable);

    QString title() const;
    void setTitle(const QString &newTitle);

    QString message() const;
    void setMessage(const QString &newMessage);

    bool admin() const;
    void setAdmin(bool newAdmin);

    bool debug() const;
    void setDebug(bool newDebug);

signals:
    void enableChanged();
    void titleChanged();
    void messageChanged();
    void adminChanged();
    void debugChanged();

private:
    bool m_enable {false};
    QString m_title;
    QString m_message;
    bool m_admin {false};
    bool m_debug {false};
};

#endif // KZALARM_H
