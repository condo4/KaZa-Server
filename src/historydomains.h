#ifndef HISTORYDOMAINS_H
#define HISTORYDOMAINS_H

#include <QSet>
#include <QString>

// Whitelist of history table families: <domain>_raw / <domain>_hourly / <domain>_daily
// must exist with the schema defined in sql/temperature.sql (or sql/tanklevel.sql, ...).
// Domain names end up interpolated into SQL table identifiers on both the read side
// (KaZaConnection::_processFrameHistory) and the write side (KaZaManager::logMeasurement),
// so only whitelisted values are ever accepted.
inline const QSet<QString> &kaZaHistoryDomains()
{
    static const QSet<QString> domains = {
        QStringLiteral("temperature"),
        QStringLiteral("tanklevel"),
    };
    return domains;
}

#endif // HISTORYDOMAINS_H
