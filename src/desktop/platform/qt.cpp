#include <QApplication>
#include <QMessageBox>

extern "C" void show_error_box(const char *message) {
    int argc = 1;
    char appName[] = "butterscotch";
    char *argv[] = { appName, nullptr };

    QApplication app(argc, argv);

    QMessageBox::critical(
        nullptr,
        "Error",
        QString::fromUtf8(message)
    );
}