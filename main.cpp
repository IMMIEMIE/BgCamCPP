#include <QApplication>
#include <QDir>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    QApplication app(argc, argv);
    BackgroundReplaceWindow window;
    window.show();
    return app.exec();
}
