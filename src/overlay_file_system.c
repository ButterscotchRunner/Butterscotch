#include "overlay_file_system.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define overlayMkdir(path) _mkdir(path)
#define overlayRmdir(path) _rmdir(path)
#else
#include <unistd.h>
#define overlayMkdir(path) mkdir((path), 0777)
#define overlayRmdir(path) rmdir(path)
#endif

// ===[ Helpers ]===

// Replaces all backslashes (\) with forward slashes (/) in a path string. (dealing with windows paths)
static inline char* normalizePath(const char* path) {
    if (path == nullptr) return nullptr;
    char* normalized = safeStrdup(path);
    for (int i = 0; normalized[i] != '\0'; i++) {
        if (normalized[i] == '\\') {
            normalized[i] = '/';
        }
    }
    return normalized;
}

static bool isAbsolute(const char* path) {
    return path != nullptr && path[0] == '/';
}

// Joins basePath (assumed to end with '/') with a relative path. Absolute inputs pass through as-is.
// Returns a heap-allocated string the caller must free.
static char* joinPath(const char* basePath, const char* normalizedPath) {
    if (isAbsolute(normalizedPath)) {
        // Absolute paths are passed through verbatim.
        return safeStrdup(normalizedPath);
    }
    size_t baseLen = strlen(basePath);
    size_t relLen = strlen(normalizedPath);
    char* full = safeMalloc(baseLen + relLen + 1);
    memcpy(full, basePath, baseLen);
    memcpy(full + baseLen, normalizedPath, relLen);
    full[baseLen + relLen] = '\0';
    return full;
}

static bool pathExists(const char* fullPath) {
    struct stat st;
    return stat(fullPath, &st) == 0;
}

// Returns a heap-allocated full path for reads. Absolute inputs pass through as-is.
// For relative inputs, savePath wins if the file exists there, else bundlePath.
static char* resolveForRead(OverlayFileSystem* ofs, const char* relativePath) {
    char* normalized = normalizePath(relativePath);
    if (isAbsolute(normalized)) return normalized;

    // Check if the path is already resolved
    if (strncmp(normalized, ofs->savePath, strlen(ofs->savePath)) == 0) return normalized;
    if (strncmp(normalized, ofs->bundlePath, strlen(ofs->bundlePath)) == 0) return normalized;

    char* saveFull = joinPath(ofs->savePath, normalized);
    if (pathExists(saveFull)) {
        free(normalized);
        return saveFull;
    }
    free(saveFull);
    char* bundleFull = joinPath(ofs->bundlePath, normalized);
    free(normalized);
    return bundleFull;
}

// Returns a heap-allocated full path for writes. Absolute inputs pass through as-is.
// For relative inputs, it will always be the save path.
static char* resolveForWrite(OverlayFileSystem* ofs, const char* relativePath) {
    char* normalized = normalizePath(relativePath);
    if (isAbsolute(normalized)) return normalized;

    // Check if the path is already resolved
    if (strncmp(normalized, ofs->savePath, strlen(ofs->savePath)) == 0) return normalized;
    if (strncmp(normalized, ofs->bundlePath, strlen(ofs->bundlePath)) == 0) return normalized;

    char* full = joinPath(ofs->savePath, normalized);
    free(normalized);
    return full;
}

// mkdir -p for the parent directory of fullPath. Used so writes to nested save paths work without the caller having to pre-create the directory tree.
static void ensureParentDir(const char* fullPath) {
    char buf[1024];
    size_t len = strlen(fullPath);
    if (len >= sizeof(buf)) return;
    memcpy(buf, fullPath, len + 1);
    char* lastSlash = strrchr(buf, '/');
    if (lastSlash == nullptr || lastSlash == buf) return;
    *lastSlash = '\0';
    // Walk forward, creating each component.
    for (size_t i = 1; len > i; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            overlayMkdir(buf);
            buf[i] = '/';
        }
    }
    overlayMkdir(buf);
}

// ===[ Vtable Implementations ]===

// Resolves a path in the "working_directory" convention (That is, it uses the bundle folder when resolving)
static char* overlayResolvePath(FileSystem* fs, const char* relativePath) {
    OverlayFileSystem* ofs = (OverlayFileSystem*) fs;
    char* normalized = normalizePath(relativePath);
    if (isAbsolute(normalized)) return normalized;

    // Check if the path is already resolved
    if (strncmp(normalized, ofs->savePath, strlen(ofs->savePath)) == 0) return normalized;
    if (strncmp(normalized, ofs->bundlePath, strlen(ofs->bundlePath)) == 0) return normalized;

    char* full = joinPath(ofs->bundlePath, normalized);
    free(normalized);
    return full;
}

static bool overlayFileExists(FileSystem* fs, const char* relativePath) {
    char* fullPath = resolveForRead((OverlayFileSystem*) fs, relativePath);
    bool exists = pathExists(fullPath);
    free(fullPath);
    return exists;
}

static char* overlayReadFileText(FileSystem* fs, const char* relativePath) {
    char* fullPath = resolveForRead((OverlayFileSystem*) fs, relativePath);
    FILE* f = fopen(fullPath, "rb");
    free(fullPath);
    if (f == nullptr) return nullptr;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = safeMalloc((size_t) size + 1);
    size_t bytesRead = fread(content, 1, (size_t) size, f);
    content[bytesRead] = '\0';
    fclose(f);
    return content;
}

static bool overlayWriteFileText(FileSystem* fs, const char* relativePath, const char* contents) {
    char* fullPath = resolveForWrite((OverlayFileSystem*) fs, relativePath);
    ensureParentDir(fullPath);
    FILE* f = fopen(fullPath, "wb");
    free(fullPath);
    if (f == nullptr) return false;

    size_t len = strlen(contents);
    size_t written = fwrite(contents, 1, len, f);
    fclose(f);
    return written == len;
}

static bool overlayDeleteFile(FileSystem* fs, const char* relativePath) {
    char* fullPath = resolveForWrite((OverlayFileSystem*) fs, relativePath);
    int result = remove(fullPath);
    free(fullPath);
    return result == 0;
}

static bool overlayReadFileBinary(FileSystem* fs, const char* relativePath, uint8_t** outData, int32_t* outSize) {
    char* fullPath = resolveForRead((OverlayFileSystem*) fs, relativePath);
    FILE* f = fopen(fullPath, "rb");
    free(fullPath);
    if (f == nullptr) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = safeMalloc((size_t) size);
    size_t bytesRead = fread(data, 1, (size_t) size, f);
    fclose(f);

    *outData = data;
    *outSize = (int32_t) bytesRead;
    return true;
}

static bool overlayWriteFileBinary(FileSystem* fs, const char* relativePath, const uint8_t* data, int32_t size) {
    char* fullPath = resolveForWrite((OverlayFileSystem*) fs, relativePath);
    ensureParentDir(fullPath);
    FILE* f = fopen(fullPath, "wb");
    free(fullPath);
    if (f == nullptr) return false;

    size_t written = fwrite(data, 1, (size_t) size, f);
    fclose(f);
    return written == (size_t) size;
}

// ===[ Streaming Binary I/O ]===
// Handle wraps the FILE* plus the resolved on-disk path

typedef struct {
    FILE* fp;
    char* fullPath; // already-resolved absolute path used at open time
} OverlayBinaryHandle;

static OverlayBinaryHandle* overlayBinaryHandleNew(FILE* fp, char* fullPathTaken) {
    OverlayBinaryHandle* h = safeMalloc(sizeof(OverlayBinaryHandle));
    h->fp = fp;
    h->fullPath = fullPathTaken; // takes ownership
    return h;
}

static void* overlayBinaryOpen(FileSystem* fs, const char* relativePath, int32_t mode) {
    OverlayFileSystem* ofs = (OverlayFileSystem*) fs;
    if (mode == GML_FILE_BIN_READ) {
        char* path = resolveForRead(ofs, relativePath);
        FILE* f = fopen(path, "rb");
        if (f == nullptr) { free(path); return nullptr; }
        return overlayBinaryHandleNew(f, path);
    }
    if (mode == GML_FILE_BIN_WRITE) {
        char* path = resolveForWrite(ofs, relativePath);
        ensureParentDir(path);
        FILE* f = fopen(path, "wb");
        if (f == nullptr) { free(path); return nullptr; }
        return overlayBinaryHandleNew(f, path);
    }
    // GML_FILE_BIN_READWRITE: preserve existing contents
    char* readPath = resolveForRead(ofs, relativePath);
    FILE* f = fopen(readPath, "r+b");
    if (f != nullptr) return overlayBinaryHandleNew(f, readPath);
    free(readPath);
    char* writePath = resolveForWrite(ofs, relativePath);
    ensureParentDir(writePath);
    f = fopen(writePath, "w+b");
    if (f == nullptr) { free(writePath); return nullptr; }
    return overlayBinaryHandleNew(f, writePath);
}

static void overlayBinaryClose(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == nullptr) return;
    OverlayBinaryHandle* h = (OverlayBinaryHandle*) handle;
    if (h->fp != nullptr) fclose(h->fp);
    free(h->fullPath);
    free(h);
}

static int32_t overlayBinaryRead(MAYBE_UNUSED FileSystem* fs, void* handle, void* dst, int32_t n) {
    if (handle == nullptr || 0 >= n) return 0;
    return (int32_t) fread(dst, 1, (size_t) n, ((OverlayBinaryHandle*) handle)->fp);
}

static int32_t overlayBinaryWrite(MAYBE_UNUSED FileSystem* fs, void* handle, const void* src, int32_t n) {
    if (handle == nullptr || 0 >= n) return 0;
    return (int32_t) fwrite(src, 1, (size_t) n, ((OverlayBinaryHandle*) handle)->fp);
}

static int32_t overlayBinaryTell(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == nullptr) return 0;
    return (int32_t) ftell(((OverlayBinaryHandle*) handle)->fp);
}

static bool overlayBinarySeek(MAYBE_UNUSED FileSystem* fs, void* handle, int32_t pos) {
    if (handle == nullptr) return false;
    return fseek(((OverlayBinaryHandle*) handle)->fp, pos, SEEK_SET) == 0;
}

static int32_t overlayBinarySize(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == nullptr) return 0;
    FILE* f = ((OverlayBinaryHandle*) handle)->fp;
    long saved = ftell(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, saved, SEEK_SET);
    return (int32_t) size;
}

static void overlayBinaryRewrite(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == nullptr) return;
    OverlayBinaryHandle* h = (OverlayBinaryHandle*) handle;
    if (h->fp != nullptr) fclose(h->fp);
    // "wb+" truncates the existing file (or creates it if it vanished since open) and gives back read+write
    h->fp = fopen(h->fullPath, "wb+");
}

static bool overlayDirectoryExists(FileSystem* fs, const char* relativePath) {
    char* fullPath = resolveForRead((OverlayFileSystem*) fs, relativePath);
    struct stat st;
    bool exists = (stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode));
    free(fullPath);
    return exists;
}

static bool overlayCreateDirectory(FileSystem* fs, const char* relativePath) {
    char* fullPath = resolveForWrite((OverlayFileSystem*) fs, relativePath);
    int result = overlayMkdir(fullPath);
    free(fullPath);
    return result == 0;
}

static bool overlayDeleteDirectory(FileSystem* fs, const char* relativePath) {
    char* fullPath = resolveForWrite((OverlayFileSystem*) fs, relativePath);
    int result = overlayRmdir(fullPath);
    free(fullPath);
    return result == 0;
}

// ===[ Vtable ]===

static FileSystemVtable overlayFileSystemVtable = {
    .resolvePath = overlayResolvePath,
    .fileExists = overlayFileExists,
    .readFileText = overlayReadFileText,
    .writeFileText = overlayWriteFileText,
    .deleteFile = overlayDeleteFile,
    .readFileBinary = overlayReadFileBinary,
    .writeFileBinary = overlayWriteFileBinary,
    .binaryOpen = overlayBinaryOpen,
    .binaryClose = overlayBinaryClose,
    .binaryRead = overlayBinaryRead,
    .binaryWrite = overlayBinaryWrite,
    .binaryTell = overlayBinaryTell,
    .binarySeek = overlayBinarySeek,
    .binarySize = overlayBinarySize,
    .binaryRewrite = overlayBinaryRewrite,
    .directoryExists = overlayDirectoryExists,
    .createDirectory = overlayCreateDirectory,
    .deleteDirectory = overlayDeleteDirectory,
};

// ===[ Lifecycle ]===

// Returns a heap-allocated copy of `path` guaranteed to end with '/'. Empty input becomes "./".
static char* withTrailingSlash(const char* path) {
    if (path == nullptr || path[0] == '\0') return safeStrdup("./");
    size_t len = strlen(path);
    char last = path[len - 1];
    if (last == '/' || last == '\\') {
        char* out = safeStrdup(path);
        if (last == '\\') out[len - 1] = '/';
        return out;
    }
    char* out = safeMalloc(len + 2);
    memcpy(out, path, len);
    out[len] = '/';
    out[len + 1] = '\0';
    return out;
}

OverlayFileSystem* OverlayFileSystem_create(const char* bundlePath, const char* savePath) {
    OverlayFileSystem* fs = safeCalloc(1, sizeof(OverlayFileSystem));
    fs->base.vtable = &overlayFileSystemVtable;
    fs->bundlePath = withTrailingSlash(bundlePath);
    fs->savePath = withTrailingSlash(savePath);
    return fs;
}

void OverlayFileSystem_destroy(OverlayFileSystem* fs) {
    if (fs == nullptr) return;
    free(fs->bundlePath);
    free(fs->savePath);
    free(fs);
}
