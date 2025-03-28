#include <QCoreApplication>
#include <QTcpSocket>
#include <QSettings>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QTcpSocket socket;
    QSettings settings("/etc/KaZaServer.conf", QSettings::IniFormat);

    QString host = settings.value("scpi/server").toString();
    if(host.isNull()) host = "127.0.0.1";

    uint16_t port = settings.value("scpi/port").toInt();
    socket.connectToHost(host, port);

    if (!socket.waitForConnected(5000)) {
        qDebug() << "Conenction error:" << socket.errorString();
        return -1;
    }

    if(argc > 1)
    {
        socket.write("obj? ");
        socket.write(argv[1]);
        socket.write("\n");
    }
    else
    {
        socket.write("obj?\n");
    }

    if (!socket.waitForBytesWritten(3000)) {
        qDebug() << "Send error:" << socket.errorString();
        socket.close();
        return -1;
    }

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
                socket.close();
                return 0;
            }
            QTextStream(stdout) << line.trimmed() << Qt::endl;
        }
    }

    socket.close();
    return a.exec();
}
