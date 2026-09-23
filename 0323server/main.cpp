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
        // Task 12 补丁 B：启动失败必须退出，不得静默假活。
        // 此前只打印错误就进入事件循环：进程存活、端口 LISTENING 但不处理任何请求，
        // 运维与测试会误判为"服务正常"。
        printf("server err\n");
        return 1;
    }



    return a.exec();
}
