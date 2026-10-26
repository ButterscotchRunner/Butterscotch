#include <windows.h>
#include <fileapifromapp.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <wchar.h>

#include "file_io.h"

static wchar_t* widePath(const char* path) {
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!len) {
        errno = EINVAL;
        return NULL;
    }

    wchar_t* wide = malloc(len * sizeof(wchar_t));
    if (!wide) {
        errno = ENOMEM;
        return NULL;
    }

    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, len);
    return wide;
}

FILE* uwpWFopen(const wchar_t* path, const wchar_t* mode) {
    if (mode[0] != L'r' || wcschr(mode, L'+')) return _wfopen(path, mode);

    HANDLE handle = CreateFile2FromAppW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
        return NULL;
    }

    int flags = _O_RDONLY | (wcschr(mode, L'b') ? _O_BINARY : _O_TEXT);
    int fd = _open_osfhandle((intptr_t)handle, flags);
    if (fd == -1) {
        CloseHandle(handle);
        return NULL;
    }

    FILE* file = _wfdopen(fd, mode);
    if (!file) _close(fd);
    return file;
}

FILE* uwpFopen(const char* path, const char* mode) {
    wchar_t* wide = widePath(path);
    if (!wide) return NULL;

    wchar_t wideMode[16];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wideMode, 16)) {
        free(wide);
        errno = EINVAL;
        return NULL;
    }

    FILE* file = uwpWFopen(wide, wideMode);
    free(wide);
    return file;
}

