#include <QCoreApplication>
#include <QTcpSocket>
#include <QSettings>
#include <QList>
#include <QString>
#include <QRegularExpression>
#include <QTextStream>

struct FilterSegment {
    QString text;
    bool isExact;  // true for quoted strings, false for fuzzy
};

QList<FilterSegment> parseFilter(const QString &filter)
{
    QList<FilterSegment> segments;
    QString currentSegment;
    bool inQuotes = false;

    for (int i = 0; i < filter.length(); ++i) {
        QChar c = filter[i];
        if (c == '"') {
            if (!currentSegment.isEmpty()) {
                FilterSegment seg;
                seg.text = currentSegment;
                seg.isExact = inQuotes;
                segments.append(seg);
                currentSegment.clear();
            }
            inQuotes = !inQuotes;
        } else {
            currentSegment += c;
        }
    }

    if (!currentSegment.isEmpty()) {
        FilterSegment seg;
        seg.text = currentSegment;
        seg.isExact = inQuotes;
        segments.append(seg);
    }

    return segments;
}

bool matchesFuzzy(const QString &name, int &pos, const QString &pattern)
{
    QString lowerPattern = pattern.toLower();
    int patternPos = 0;

    while (patternPos < lowerPattern.length() && pos < name.length()) {
        if (name[pos] == lowerPattern[patternPos]) {
            patternPos++;
        }
        pos++;
    }

    return patternPos == lowerPattern.length();
}

bool matchesExact(const QString &name, int &pos, const QString &pattern)
{
    QString lowerPattern = pattern.toLower();
    int foundPos = name.indexOf(lowerPattern, pos, Qt::CaseInsensitive);

    if (foundPos == -1) {
        return false;
    }

    pos = foundPos + lowerPattern.length();
    return true;
}

bool matchesFilter(const QString &name, const QString &filter)
{
    if (filter.isEmpty()) {
        return true;
    }

    QString lowerName = name.toLower();
    QList<FilterSegment> segments = parseFilter(filter);

    int pos = 0;
    for (const FilterSegment &segment : segments) {
        if (segment.isExact) {
            if (!matchesExact(lowerName, pos, segment.text)) {
                return false;
            }
        } else {
            if (!matchesFuzzy(lowerName, pos, segment.text)) {
                return false;
            }
        }
    }

    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QTcpSocket socket;
    QSettings settings("/etc/kazad.conf", QSettings::IniFormat);

    QString host = settings.value("control/server").toString();
    if(host.isNull()) host = "127.0.0.1";

    uint16_t port = settings.value("control/port").toInt();
    socket.connectToHost(host, port);

    if (!socket.waitForConnected(5000)) {
        qDebug() << "Conenction error:" << socket.errorString();
        return -1;
    }

    // Determine if we need to filter client-side
    bool useFilter = false;
    QString filterPattern;

    if(argc > 1)
    {
        QString arg = QString::fromUtf8(argv[1]);
        if (arg.startsWith("/")) {
            // Filter mode: fetch all objects and filter client-side
            useFilter = true;
            filterPattern = arg.mid(1);  // Remove the leading "/"
            socket.write("obj?\n");
        } else {
            // Specific object mode: query server for this object
            socket.write("obj? ");
            socket.write(arg.toUtf8());
            socket.write("\n");
        }
    }
    else
    {
        // No argument: list all objects
        socket.write("obj?\n");
    }

    if (!socket.waitForBytesWritten(3000)) {
        qDebug() << "Send error:" << socket.errorString();
        socket.close();
        return -1;
    }

    // Read response
    QList<QString> lines;
    while (true) {
        if (!socket.waitForReadyRead(3000)) {
            if (socket.error() != QAbstractSocket::SocketTimeoutError) {
                qDebug() << "Read error:" << socket.errorString();
            }
            break;
        }

        while (socket.canReadLine()) {
            QByteArray line = socket.readLine();
            if (line == "\n" ) {
                // End of response
                socket.close();

                // If filtering, apply filter now
                if (useFilter) {
                    for (const QString &l : lines) {
                        // Extract object ID (first word before whitespace)
                        QString objectId = l.split(QRegularExpression("\\s+")).first();
                        if (matchesFilter(objectId, filterPattern)) {
                            QTextStream(stdout) << l << Qt::endl;
                        }
                    }
                } else {
                    // No filtering, print all lines
                    for (const QString &l : lines) {
                        QTextStream(stdout) << l << Qt::endl;
                    }
                }

                return 0;
            }
            lines.append(QString::fromUtf8(line.trimmed()));
        }
    }

    socket.close();
    return a.exec();
}
