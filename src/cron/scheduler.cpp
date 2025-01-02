#include "scheduler.h"
#include <QDateTime>
#include <qdebug.h>

Scheduler *Scheduler::m_instance = nullptr;

Scheduler::Scheduler(QObject *parent)
    : QObject(parent)
{
    m_instance = this;
    m_tick = QDateTime::currentDateTime().time().second();
    QObject::connect(&m_timer, &QTimer::timeout, this, &Scheduler::_tick);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    m_timer.setInterval(1000);
    m_timer.start();
}

Scheduler *Scheduler::getInstance()
{
    return m_instance;
}

void Scheduler::_tick()
{
    m_tick++;
    emit tick();
    if(m_tick % 60 == 0)
        emit tickMinute();
}
