#include <QCoreApplication>
#include <QSslSocket>
#include <QSettings>
#include <QList>
#include <QString>
#include <QRegularExpression>
#include <QTextStream>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslCertificate>
#include <QFile>
#include <QTimer>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include "../protocol/kazaprotocol.h"

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

void printObjects(const QMap<QString, QPair<QVariant, QString>> &objects, const QString &filterPattern, const QString &specificObject)
{
    QTextStream out(stdout);

    if (!specificObject.isEmpty()) {
        // Print specific object
        if (objects.contains(specificObject)) {
            const auto &data = objects[specificObject];
            QString line = specificObject.leftJustified(80, ' ');
            line.append(data.first.toString());
            line.append(" ");
            line.append(data.second);
            out << line << Qt::endl;
        }
    } else if (!filterPattern.isEmpty()) {
        // Print filtered objects
        for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
            const QString &name = it.key();
            if (matchesFilter(name, filterPattern)) {
                const auto &data = it.value();
                QString line = name.leftJustified(80, ' ');
                line.append(data.first.toString());
                line.append(" ");
                line.append(data.second);
                out << line << Qt::endl;
            }
        }
    } else {
        // Print all objects
        for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
            const QString &name = it.key();
            const auto &data = it.value();
            QString line = name.leftJustified(80, ' ');
            line.append(data.first.toString());
            line.append(" ");
            line.append(data.second);
            out << line << Qt::endl;
        }
    }
}

// Control port mode (simple SSL, text-based protocol)
int controlPortMode(const QString &filterPattern, const QString &specificObject)
{
    QSslSocket socket;
    QSettings settings("/etc/kazad.conf", QSettings::IniFormat);

    QString host = settings.value("control/server").toString();
    if(host.isNull()) host = "127.0.0.1";

    uint16_t port = settings.value("control/port").toInt();

    // Configure SSL to ignore certificate errors for self-signed certificates
    QSslConfiguration sslConfig = socket.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    socket.setSslConfiguration(sslConfig);

    // Ignore SSL errors to allow self-signed certificates
    QObject::connect(&socket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
                     [&socket](const QList<QSslError> &errors) {
                         socket.ignoreSslErrors();
                     });

    socket.connectToHostEncrypted(host, port);

    if (!socket.waitForConnected(5000)) {
        qDebug() << "Connection error:" << socket.errorString();
        return -1;
    }

    // Wait for SSL handshake to complete
    if (!socket.waitForEncrypted(5000)) {
        qDebug() << "SSL handshake error:" << socket.errorString();
        return -1;
    }

    // Send command based on mode
    if (!specificObject.isEmpty()) {
        socket.write("obj? ");
        socket.write(specificObject.toUtf8());
        socket.write("\n");
    } else {
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

                // Print results
                QTextStream out(stdout);
                if (!filterPattern.isEmpty()) {
                    for (const QString &l : lines) {
                        QString objectId = l.split(QRegularExpression("\\s+")).first();
                        if (matchesFilter(objectId, filterPattern)) {
                            out << l << Qt::endl;
                        }
                    }
                } else {
                    for (const QString &l : lines) {
                        out << l << Qt::endl;
                    }
                }

                return 0;
            }
            lines.append(QString::fromUtf8(line.trimmed()));
        }
    }

    socket.close();
    return -1;
}

// Main SSL port mode (with client certificates and binary protocol)
int sslPortMode(const QString &filterPattern, const QString &specificObject)
{
    QSslSocket socket;

    // Read configuration from KaZa-Control-Panel.conf
    QString configPath = QDir::homePath() + "/.config/KaZoe/KaZa-Control-Panel.conf";
    QSettings settings(configPath, QSettings::IniFormat);

    if (!settings.contains("ssl/host")) {
        qDebug() << "Error: SSL configuration not found in" << configPath;
        qDebug() << "Please run KaZa-Control-Panel first to configure the connection";
        return -1;
    }

    QString host = settings.value("ssl/host").toString();
    int port = settings.value("ssl/port", 1756).toInt();
    QString caCertPath = settings.value("ssl/cacert").toString();
    QString clientCertPath = settings.value("ssl/client_cert").toString();
    QString clientKeyPath = settings.value("ssl/client_key").toString();
    QString clientPass = settings.value("ssl/client_pass").toString();

    // Load CA certificate
    QFile caCertFile(caCertPath);
    if (!caCertFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open CA certificate:" << caCertPath;
        return -1;
    }
    QList<QSslCertificate> caCerts = QSslCertificate::fromDevice(&caCertFile, QSsl::Pem);
    caCertFile.close();

    if (caCerts.isEmpty()) {
        qDebug() << "No CA certificates found in" << caCertPath;
        return -1;
    }

    // Load client certificate
    QFile clientCertFile(clientCertPath);
    if (!clientCertFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open client certificate:" << clientCertPath;
        return -1;
    }
    QList<QSslCertificate> clientCerts = QSslCertificate::fromDevice(&clientCertFile, QSsl::Pem);
    clientCertFile.close();

    if (clientCerts.isEmpty()) {
        qDebug() << "No client certificates found in" << clientCertPath;
        return -1;
    }

    // Load client key
    QFile clientKeyFile(clientKeyPath);
    if (!clientKeyFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open client key:" << clientKeyPath;
        return -1;
    }
    QSslKey privateKey(&clientKeyFile, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, clientPass.toUtf8());
    clientKeyFile.close();

    if (privateKey.isNull()) {
        qDebug() << "Failed to load private key (check password)";
        return -1;
    }

    // Configure SSL
    QSslConfiguration sslConfig = socket.sslConfiguration();
    sslConfig.setCaCertificates(caCerts);
    sslConfig.setLocalCertificate(clientCerts.first());
    sslConfig.setPrivateKey(privateKey);
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    socket.setSslConfiguration(sslConfig);

    // Create protocol handler
    KaZaProtocol protocol(&socket);

    // State tracking
    bool versionNegotiated = false;
    bool objectsListReceived = false;
    QMap<QString, QPair<QVariant, QString>> objectsList;
    int exitCode = -1;

    // Connect protocol signals
    QObject::connect(&protocol, &KaZaProtocol::versionNegotiated, [&]() {
        versionNegotiated = true;
        // Request objects list (server sends ALL objects, no subscription needed)
        protocol.sendCommand("OBJLIST?");
    });

    QObject::connect(&protocol, &KaZaProtocol::versionIncompatible, [&](QString reason) {
        socket.disconnectFromHost();
        exitCode = -1;
    });

    QObject::connect(&protocol, &KaZaProtocol::frameObjectsList,
                     [&](const QMap<QString, QPair<QVariant, QString>> &objects) {
        objectsListReceived = true;
        objectsList = objects;

        // Print objects
        printObjects(objectsList, filterPattern, specificObject);

        // Disconnect
        socket.disconnectFromHost();
        exitCode = 0;
    });

    // SSL connection signals
    QObject::connect(&socket, &QSslSocket::encrypted, [&]() {
        // Send version negotiation
        protocol.sendVersion();
    });

    QObject::connect(&socket, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                     [&](const QList<QSslError> &errors) {
        socket.ignoreSslErrors();
    });

    QObject::connect(&socket, &QSslSocket::disconnected, [&]() {
        QCoreApplication::quit();
    });

    // Setup timeout
    QTimer::singleShot(10000, [&]() {
        if (!objectsListReceived) {
            socket.disconnectFromHost();
            exitCode = -1;
        }
    });

    // Connect to server
    socket.connectToHostEncrypted(host, port);

    if (!socket.waitForConnected(5000)) {
        qDebug() << "Connection error:" << socket.errorString();
        return -1;
    }

    if (!socket.waitForEncrypted(5000)) {
        qDebug() << "SSL handshake error:" << socket.errorString();
        return -1;
    }

    // Run event loop to process protocol messages
    QCoreApplication::exec();

    return exitCode;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    // Parse arguments
    bool useSslPort = false;
    bool useFilter = false;
    QString filterPattern;
    QString specificObject;

    int argStart = 1;
    if (argc > 1 && QString::fromUtf8(argv[1]) == "--ssl") {
        useSslPort = true;
        argStart = 2;
    }

    if (argc > argStart) {
        QString arg = QString::fromUtf8(argv[argStart]);
        if (arg.startsWith("/")) {
            useFilter = true;
            filterPattern = arg.mid(1);
        } else {
            specificObject = arg;
        }
    }

    // Choose mode
    if (useSslPort) {
        return sslPortMode(filterPattern, specificObject);
    } else {
        return controlPortMode(filterPattern, specificObject);
    }
}
