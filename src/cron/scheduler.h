#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <QObject>
#include <QTimer>

class Scheduler : public QObject
{
    Q_OBJECT
    QTimer m_timer;
    int m_tick;

    static Scheduler *m_instance;
public:
    explicit Scheduler(QObject *parent = nullptr);
    static Scheduler *getInstance();

private slots:
    void _tick();

signals:
    void tick();
    void tickMinute();

};

#endif // SCHEDULER_H
