#include <QCoreApplication>
#include "kernel/tcpkernel.h"
#include "LogManager.h"
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    LogManager::init("server");

    Ikernel*p=tcpkernel::getKernel();
    if(p->boolopen())
    {
        printf("server is running\n");
    }
    else
    {
        printf("server err");
    }



    return a.exec();
}
