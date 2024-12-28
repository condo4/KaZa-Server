#include <QCoreApplication>
#include "kazamanager.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    KaZaManager manager;

    return a.exec();
}
