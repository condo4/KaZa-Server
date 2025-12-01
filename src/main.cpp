#include <QCoreApplication>
#include "kazamanager.h"
#include <systemd/sd-daemon.h>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setOrganizationName("KaZoe");
    a.setOrganizationDomain("kaza.kazoe.org");
    a.setApplicationName("kazad");
    KaZaManager manager;

    if (!manager.isInitialized()) {
        qCritical() << "KaZa Server failed to initialize - exiting";
        return 1;
    }

    sd_notify(0, "READY=1");
    return a.exec();
}
