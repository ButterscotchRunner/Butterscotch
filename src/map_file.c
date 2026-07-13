#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
    #if defined(_POSIX_MAPPED_FILES) && (_POSIX_MAPPED_FILES > 0)
        #include <sys/mman.h>
    #endif
#endif

uint8_t *mapFile(FILE *file, size_t size) {
    if (!file || size == 0) return NULL;

#if defined(_WIN32)
    intptr_t osHandle = _get_osfhandle(_fileno(file));
    if (osHandle == -1) return NULL;
    HANDLE hFile = (HANDLE)osHandle;

    HANDLE hMap = CreateFileMappingA(
        hFile,
        NULL,
        PAGE_READONLY,
        0, 0,
        NULL
    );
    if (!hMap) return NULL;

    void *ptr = MapViewOfFile(
        hMap,
        FILE_MAP_READ,
        0, 0,
        size
    );
    CloseHandle(hMap);

    if (!ptr) return NULL;
    return (uint8_t *)ptr;
#elif defined(_POSIX_MAPPED_FILES) && _POSIX_MAPPED_FILES > 0
    int fd = fileno(file);
    if (fd == -1) return NULL;

    void *ptr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) return NULL;

    return (uint8_t *)ptr;
#else
    return NULL;
#endif
}

void unmapFile(uint8_t *ptr, size_t size) {
    if (!ptr) return;

#ifdef _WIN32
    (void)size;
    UnmapViewOfFile((LPCVOID)ptr);
#elif defined(_POSIX_MAPPED_FILES) && _POSIX_MAPPED_FILES > 0
    munmap(ptr, size);
#endif
}
