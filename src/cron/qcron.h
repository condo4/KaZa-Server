#ifndef _QCRON_HPP
#define _QCRON_HPP

#include <QObject>
#include <QDateTime>
#include "qcronfield.h"
#include <qqml.h>



class QCron : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Cron)


    Q_PROPERTY(QString pattern READ pattern WRITE setPattern NOTIFY patternChanged)

public:
    QCron();
    QCron(const QString & pattern);
    ~QCron();

    // Accessors.
    bool isValid() const;

    const QString & error() const;

    // Features.

    QDateTime next();
    QDateTime next(QDateTime dt);
    void catchUp(QDateTime & dt, EField field, int value);
    bool match(const QDateTime & dt) const;
    void add(QDateTime & dt, EField field, int value);

    QString pattern() const;

public slots:
    void setPattern(QString pattern);

signals:
    void activated();
    void deactivated();

    void patternChanged(QString pattern);

private:
    bool _is_valid {true};
    bool _is_active {false};
    QCronField _fields[6] {EField::MINUTE, EField::HOUR, EField::DOM, EField::MONTH, EField::DOW, EField::YEAR};

    void _parsePattern(const QString & pattern);
    void _parseField(QString & field_str,
                     EField field);
    QString _validCharacters(EField field);
    void _process(QDateTime & dt, EField field);

    QString m_pattern;

private slots:
    void _checkState();
};

#endif
