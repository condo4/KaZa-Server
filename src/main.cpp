#include <QCoreApplication>
#include "kazamanager.h"
#include <systemd/sd-daemon.h>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    KaZaManager manager;

    sd_notify(0, "READY=1");
    return a.exec();
}
