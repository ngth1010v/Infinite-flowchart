#include "MainWindow.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Flowchart");
    QApplication::setOrganizationName("Flowchart");
    QApplication::setStyle("Fusion");
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && args[1] == "--export") return MainWindow::runCli(args);  // command line, no window

    MainWindow w;
    w.resize(1440, 900);
    w.show();
    w.start(args.size() > 1 ? args[1] : QString());  // double-clicked .flowx file
    return app.exec();
}
