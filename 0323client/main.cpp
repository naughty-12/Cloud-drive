#include "widget.h"

#include <QApplication>
#include "LogManager.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    LogManager::init("client");
    Widget w;
    //w.show();
    w.hide();
    return a.exec();
}
