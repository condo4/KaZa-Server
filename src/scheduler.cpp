#include "scheduler.h"
#include <QTimer>
#include <QDateTime>
#include <QDebug>

class SchedulerFilter {
protected:
    QString m_param;
public:
    SchedulerFilter(const QString &param): m_param(param) {}
    virtual ~SchedulerFilter() = default;
    virtual bool match(int value) = 0;
    QString param() const { return m_param; }
};

class SchedulerFilterAny: public SchedulerFilter {
public:
    SchedulerFilterAny(QString param): SchedulerFilter(param) {}
    virtual ~SchedulerFilterAny() = default;
    bool match(int value) override {
        return true;
    }
};

class SchedulerFilterConstant: public SchedulerFilter {
    int m_value;

public:
    SchedulerFilterConstant(QString param): SchedulerFilter(param) {
        m_value = param.toInt();
    }
    virtual ~SchedulerFilterConstant() = default;
    bool match(int value) override {
        return value == m_value;
    }
};

class SchedulerFilterRange: public SchedulerFilter {
    int m_min_value;
    int m_max_value;

public:
    SchedulerFilterRange(QString param): SchedulerFilter(param) {
        QStringList tab = param.split("-");
        m_min_value = tab[0].toInt();
        m_max_value = tab[1].toInt();
    }
    virtual ~SchedulerFilterRange() = default;
    bool match(int value) override {
        return value >= m_min_value && value <= m_max_value;
    }
};


class SchedulerFilterList: public SchedulerFilter {
    QList<int> m_values;

public:
    SchedulerFilterList(QString param): SchedulerFilter(param) {
        QStringList tab = param.split(",");
        for(const QString &i: tab)
        {
            m_values.append(i.toInt());
        }
    }
    virtual ~SchedulerFilterList() = default;
    bool match(int value) override {
        return m_values.contains(value);
    }
};

class SchedulerFilterModulo: public SchedulerFilter {
    int m_modulo;

public:
    SchedulerFilterModulo(QString param): SchedulerFilter(param) {
        QStringList tab = param.split("/");
        m_modulo = tab[1].toInt();
    }
    virtual ~SchedulerFilterModulo() = default;
    bool match(int value) override {
        return value % m_modulo == 0;
    }
};

QSharedPointer<SchedulerFilter> createFilter(QString param) {
    if(param == "*")
        return QSharedPointer<SchedulerFilterAny>::create(param);
    if(param.contains("-"))
        return QSharedPointer<SchedulerFilterRange>::create(param);
    if(param.contains(","))
        return QSharedPointer<SchedulerFilterList>::create(param);
    if(param.contains("/"))
        return QSharedPointer<SchedulerFilterModulo>::create(param);
    return QSharedPointer<SchedulerFilterConstant>::create(param);
}

class SchedulerPrivate {
    Q_DISABLE_COPY(SchedulerPrivate)
    Q_DECLARE_PUBLIC(Scheduler)
    Scheduler * const q_ptr;
    SchedulerPrivate(Scheduler*);

    QTimer m_timer;
    bool m_ready {false};
    QSharedPointer<SchedulerFilter> m_filter_year;
    QSharedPointer<SchedulerFilter> m_filter_month;
    QSharedPointer<SchedulerFilter> m_filter_day;
    QSharedPointer<SchedulerFilter> m_filter_wday;
    QSharedPointer<SchedulerFilter> m_filter_hour;
    QSharedPointer<SchedulerFilter> m_filter_minute;
};

SchedulerPrivate::SchedulerPrivate(Scheduler *parent)
    : q_ptr(parent)
{
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, q_ptr, &Scheduler::__tick);
    m_timer.setSingleShot(true);
    m_timer.start((60 - QDateTime::currentDateTime().time().second()) * 1000);
}


Scheduler::Scheduler(QObject *parent)
    : QObject{parent}
    , d_ptr(new SchedulerPrivate(this))
{
    QObject::connect(this, &Scheduler::minuteChanged, this, &Scheduler::paternChanged);
    QObject::connect(this, &Scheduler::hourChanged, this, &Scheduler::paternChanged);
    QObject::connect(this, &Scheduler::dayChanged, this, &Scheduler::paternChanged);
    QObject::connect(this, &Scheduler::monthChanged, this, &Scheduler::paternChanged);
    QObject::connect(this, &Scheduler::wdayChanged, this, &Scheduler::paternChanged);
}

void Scheduler::__tick()
{
    Q_D(Scheduler);
    QDateTime now = QDateTime::currentDateTime();
    d->m_timer.start((60 - now.time().second()) * 1000);

    if(!d->m_filter_year.isNull() && !d->m_filter_year->match(now.date().year()))
        return;
    if(!d->m_filter_month.isNull() && !d->m_filter_month->match(now.date().month()))
        return;
    if(!d->m_filter_day.isNull() && !d->m_filter_day->match(now.date().day()))
        return;
    if(!d->m_filter_wday.isNull() && !d->m_filter_wday->match(now.date().dayOfWeek()))
        return;
    if(!d->m_filter_hour.isNull() && !d->m_filter_hour->match(now.time().hour()))
        return;
    if(!d->m_filter_minute.isNull() && !d->m_filter_minute->match(now.time().minute()))
        return;

    emit timeout();
}

Scheduler::~Scheduler() {}

QString Scheduler::year() const
{
    Q_D(const Scheduler);
    if(d->m_filter_year.isNull()) return "*";
    return d->m_filter_year->param();
}

void Scheduler::setYear(const QString &newYear)
{
    Q_D(Scheduler);
    if (!d->m_filter_year.isNull() && d->m_filter_year->param() == newYear)
        return;
    d->m_filter_year = createFilter(newYear);
    emit yearChanged();
}

QString Scheduler::month() const
{
    Q_D(const Scheduler);
    if(d->m_filter_month.isNull()) return "*";
    return d->m_filter_month->param();
}

void Scheduler::setMonth(const QString &newMonth)
{
    Q_D(Scheduler);
    if (!d->m_filter_month.isNull() && d->m_filter_month->param() == newMonth)
        return;
    d->m_filter_month = createFilter(newMonth);
    emit monthChanged();
}

QString Scheduler::day() const
{
    Q_D(const Scheduler);
    if(d->m_filter_day.isNull()) return "*";
    return d->m_filter_day->param();
}

void Scheduler::setDay(const QString &newDay)
{
    Q_D(Scheduler);
    if (!d->m_filter_day.isNull() && d->m_filter_day->param() == newDay)
        return;
    d->m_filter_day = createFilter(newDay);
    emit dayChanged();
}

QString Scheduler::wday() const
{
    Q_D(const Scheduler);
    if(d->m_filter_wday.isNull()) return "*";
    return d->m_filter_wday->param();
}

void Scheduler::setWday(const QString &newWday)
{
    Q_D(Scheduler);
    if (!d->m_filter_wday.isNull() && d->m_filter_wday->param() == newWday)
        return;
    d->m_filter_wday = createFilter(newWday);
    emit dayChanged();
}

QString Scheduler::hour() const
{
    Q_D(const Scheduler);
    if(d->m_filter_hour.isNull()) return "*";
    return d->m_filter_hour->param();
}

void Scheduler::setHour(const QString &newHour)
{
    Q_D(Scheduler);
    if (!d->m_filter_hour.isNull() && d->m_filter_hour->param() == newHour)
        return;
    d->m_filter_hour = createFilter(newHour);
    emit hourChanged();
}

QString Scheduler::minute() const
{
    Q_D(const Scheduler);
    if(d->m_filter_minute.isNull()) return "*";
    return d->m_filter_minute->param();
}

void Scheduler::setMinute(const QString &newMinute)
{
    Q_D(Scheduler);
    if (!d->m_filter_minute.isNull() && d->m_filter_minute->param() == newMinute)
        return;
    d->m_filter_minute = createFilter(newMinute);
    emit minuteChanged();
}

QString Scheduler::patern() const
{
    return minute() + " " + hour() + " " + day() + " " + month() + " " + wday();
}

void Scheduler::setPatern(const QString &newPatern)
{
    if (patern() == newPatern)
        return;
    QStringList tab = newPatern.split(" ");
    if(tab.size() != 5)
    {
        qWarning() << "Pattern error: " << newPatern << " (should be 5 fields)";
        return;
    }
    setMinute(tab[0]);
    setHour(tab[1]);
    setDay(tab[2]);
    setMonth(tab[3]);
    setWday(tab[4]);
}
