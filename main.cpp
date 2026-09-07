#include "mainwindow.h"

#include <QApplication>
#include <QtCore/QCoreApplication>

int main(int argc, char *argv[])
{
    // Ubuntu 虚拟机无 GPU 驱动时，强制软件 OpenGL，避免 QtCharts 因 EGL 初始化失败而崩溃
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
