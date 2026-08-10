#include <QApplication>
#include <QMessageBox>

extern "C" void show_error_box(const char *message) {
    QMessageBox::critical(
        nullptr,
        "Error",
        QString::fromUtf8(message)
    );
}