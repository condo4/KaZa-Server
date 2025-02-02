#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <QObject>

class SchedulerPrivate;

class Scheduler : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(Scheduler)
    QScopedPointer<SchedulerPrivate> const d_ptr;

    Q_PROPERTY(QString year READ year WRITE setYear NOTIFY yearChanged)
    Q_PROPERTY(QString month READ month WRITE setMonth NOTIFY monthChanged)
    Q_PROPERTY(QString day READ day WRITE setDay NOTIFY dayChanged)
    Q_PROPERTY(QString wday READ wday WRITE setWday NOTIFY wdayChanged)
    Q_PROPERTY(QString hour READ hour WRITE setHour NOTIFY hourChanged)
    Q_PROPERTY(QString minute READ minute WRITE setMinute NOTIFY minuteChanged)
    Q_PROPERTY(QString patern READ patern WRITE setPatern NOTIFY paternChanged FINAL)

public:
    explicit Scheduler(QObject *parent = nullptr);
    virtual ~Scheduler();

    QString year() const;
    void setYear(const QString &newYear);

    QString month() const;
    void setMonth(const QString &newMonth);

    QString day() const;
    void setDay(const QString &newDay);

    QString wday() const;
    void setWday(const QString &newWday);

    QString hour() const;
    void setHour(const QString &newHour);

    QString minute() const;
    void setMinute(const QString &newMinute);

    QString patern() const;
    void setPatern(const QString &newPatern);

signals:
    void yearChanged();
    void monthChanged();
    void dayChanged();
    void wdayChanged();
    void hourChanged();
    void minuteChanged();
    void timeout();

    void paternChanged();

private slots:
    void __tick();
};

#endif // SCHEDULER_H
