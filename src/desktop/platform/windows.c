#include <windows.h>

void show_error_box(const char *message) {
    MessageBoxA(NULL, message, "Error", MB_OK | MB_ICONERROR);
}