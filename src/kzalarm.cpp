#include "kzalarm.h"
#include "kazamanager.h"

KzAlarm::KzAlarm(QObject *parent)
    : QObject{parent}
{
    KaZaManager::registerAlarm(this);
}

bool KzAlarm::enable() const
{
    return m_enable;
}

void KzAlarm::setEnable(bool newEnable)
{
    if (m_enable == newEnable)
        return;
    m_enable = newEnable;
    emit enableChanged();
}

QString KzAlarm::title() const
{
    return m_title;
}

void KzAlarm::setTitle(const QString &newTitle)
{
    if (m_title == newTitle)
        return;
    m_title = newTitle;
    emit titleChanged();
}

QString KzAlarm::message() const
{
    return m_message;
}

void KzAlarm::setMessage(const QString &newMessage)
{
    if (m_message == newMessage)
        return;
    m_message = newMessage;
    emit messageChanged();
}

bool KzAlarm::admin() const
{
    return m_admin;
}

void KzAlarm::setAdmin(bool newAdmin)
{
    if (m_admin == newAdmin)
        return;
    m_admin = newAdmin;
    emit adminChanged();
}

bool KzAlarm::debug() const
{
    return m_debug;
}

void KzAlarm::setDebug(bool newDebug)
{
    if (m_debug == newDebug)
        return;
    m_debug = newDebug;
    emit debugChanged();
}
