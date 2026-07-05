#include "noop_file_system.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include "stb_ds.h"

// ===[ In-Memory File Storage ]===

typedef struct {
    char* key; // file path
    char* value; // file contents
} MemoryFileEntry;

typedef struct {
    uint8_t* data;
    int32_t size;
} MemoryBinaryData;

typedef struct {
    char* key; // file path
    MemoryBinaryData value;
} MemoryBinaryEntry;

typedef struct {
    char* key; // directory path
    bool value; // always true if it exists
} MemoryDirEntry;

typedef struct {
    FileSystem base;
    MemoryFileEntry* files; // stb_ds string hashmap
    MemoryBinaryEntry* binaryFiles; // stb_ds string hashmap
    MemoryDirEntry* directories; // stb_ds string hashmap
} NoopFileSystem;

// ===[ Vtable Implementations ]===

static char* noopResolvePath(MAYBE_UNUSED FileSystem* fs, MAYBE_UNUSED const char* relativePath) {
    return safeStrdup("./");
}

static bool noopFileExists(FileSystem* fs, const char* relativePath) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    return shgeti(nfs->files, relativePath) >= 0;
}

static char* noopReadFileText(FileSystem* fs, const char* relativePath) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    ptrdiff_t idx = shgeti(nfs->files, relativePath);
    if (0 > idx)
        return nullptr;
    return safeStrdup(nfs->files[idx].value);
}

static bool noopWriteFileText(FileSystem* fs, const char* relativePath, const char* contents) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;

    // If the key already exists, free the old value before overwriting
    ptrdiff_t idx = shgeti(nfs->files, relativePath);
    if (idx >= 0) {
        free(nfs->files[idx].value);
        nfs->files[idx].value = safeStrdup(contents);
    } else {
        shput(nfs->files, relativePath, safeStrdup(contents));
    }

    return true;
}

static bool noopDeleteFile(FileSystem* fs, const char* relativePath) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    ptrdiff_t idx = shgeti(nfs->files, relativePath);
    if (0 > idx)
        return false;

    free(nfs->files[idx].value);
    shdel(nfs->files, relativePath);
    return true;
}

static bool noopReadFileBinary(FileSystem* fs, const char* relativePath, uint8_t** outData, int32_t* outSize) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    ptrdiff_t idx = shgeti(nfs->binaryFiles, relativePath);
    if (0 > idx)
        return false;

    MemoryBinaryData* entry = &nfs->binaryFiles[idx].value;
    uint8_t* copy = (uint8_t *)safeMalloc((size_t) entry->size);
    memcpy(copy, entry->data, (size_t) entry->size);
    *outData = copy;
    *outSize = entry->size;
    return true;
}

static bool noopWriteFileBinary(FileSystem* fs, const char* relativePath, const uint8_t* data, int32_t size) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;

    ptrdiff_t idx = shgeti(nfs->binaryFiles, relativePath);
    if (idx >= 0) {
        free(nfs->binaryFiles[idx].value.data);
        uint8_t* copy = (uint8_t *)safeMalloc((size_t) size);
        memcpy(copy, data, (size_t) size);
        nfs->binaryFiles[idx].value.data = copy;
        nfs->binaryFiles[idx].value.size = size;
    } else {
        uint8_t* copy = (uint8_t *)safeMalloc((size_t) size);
        memcpy(copy, data, (size_t) size);
        MemoryBinaryData binaryData = {0};
        binaryData.data = copy;
        binaryData.size = size;
        shput(nfs->binaryFiles, relativePath, binaryData);
    }

    return true;
}

// ===[ Streaming Binary I/O ]===

typedef struct {
    NoopFileSystem* owner;
    char* path; // strdup of relative path (so we know where to flush)
    uint8_t* buffer; // working copy (mutable for write modes)
    int32_t size;
    int32_t capacity;
    int32_t position;
    bool writable;
    bool dirty;
} NoopBinaryHandle;

static void* noopBinaryOpen(FileSystem* fs, const char* relativePath, int32_t mode) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    NoopBinaryHandle* h = (NoopBinaryHandle *)safeCalloc(1, sizeof(NoopBinaryHandle));
    h->owner = nfs;
    h->path = safeStrdup(relativePath);
    h->writable = (mode != GML_FILE_BIN_READ);

    if (mode == GML_FILE_BIN_WRITE) {
        // Truncate: empty buffer, dirty so close() always writes (even if no bytes added)
        h->dirty = true;
    } else {
        ptrdiff_t idx = shgeti(nfs->binaryFiles, relativePath);
        if (idx >= 0) {
            MemoryBinaryData* entry = &nfs->binaryFiles[idx].value;
            h->buffer = (uint8_t *)safeMalloc((size_t) entry->size);
            memcpy(h->buffer, entry->data, (size_t) entry->size);
            h->size = entry->size;
            h->capacity = entry->size;
        } else if (mode == GML_FILE_BIN_READ) {
            // Pure read of a missing file: fail
            free(h->path);
            free(h);
            return nullptr;
        }
    }
    return h;
}

static void noopBinaryClose(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == nullptr) return;
    NoopBinaryHandle* h = (NoopBinaryHandle*) handle;
    if (h->dirty) {
        ptrdiff_t idx = shgeti(h->owner->binaryFiles, h->path);
        if (idx >= 0) {
            free(h->owner->binaryFiles[idx].value.data);
            h->owner->binaryFiles[idx].value.data = h->buffer;
            h->owner->binaryFiles[idx].value.size = h->size;
        } else {
            MemoryBinaryData data = {0};
            data.data = h->buffer;
            data.size = h->size;
            shput(h->owner->binaryFiles, h->path, data);
        }
        // Map now owns the buffer
    } else {
        free(h->buffer);
    }
    free(h->path);
    free(h);
}

static int32_t noopBinaryRead(MAYBE_UNUSED FileSystem* fs, void* handle, void* dst, int32_t n) {
    if (handle == nullptr || 0 >= n) return 0;
    NoopBinaryHandle* h = (NoopBinaryHandle*) handle;
    int32_t avail = h->size - h->position;
    if (0 >= avail) return 0;
    if (n > avail) n = avail;
    memcpy(dst, h->buffer + h->position, (size_t) n);
    h->position += n;
    return n;
}

static int32_t noopBinaryWrite(MAYBE_UNUSED FileSystem* fs, void* handle, const void* src, int32_t n) {
    if (handle == nullptr || 0 >= n) return 0;
    NoopBinaryHandle* h = (NoopBinaryHandle*) handle;
    if (!h->writable) return 0;
    int32_t needed = h->position + n;
    if (needed > h->capacity) {
        int32_t newCap = h->capacity > 0 ? h->capacity : 64;
        while (newCap < needed) newCap *= 2;
        h->buffer = (uint8_t *)safeRealloc(h->buffer, (size_t) newCap);
        h->capacity = newCap;
    }
    if (h->position > h->size) {
        memset(h->buffer + h->size, 0, (size_t) (h->position - h->size));
    }
    memcpy(h->buffer + h->position, src, (size_t) n);
    h->position += n;
    if (h->position > h->size) h->size = h->position;
    h->dirty = true;
    return n;
}

static int32_t noopBinaryTell(MAYBE_UNUSED FileSystem* fs, void* handle) {
    return handle != nullptr ? ((NoopBinaryHandle*) handle)->position : 0;
}

static bool noopBinarySeek(MAYBE_UNUSED FileSystem* fs, void* handle, int32_t pos) {
    if (handle == nullptr) return false;
    if (0 > pos) pos = 0;
    ((NoopBinaryHandle*) handle)->position = pos;
    return true;
}

static int32_t noopBinarySize(MAYBE_UNUSED FileSystem* fs, void* handle) {
    return handle != nullptr ? ((NoopBinaryHandle*) handle)->size : 0;
}

static void noopBinaryRewrite(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == nullptr) return;
    NoopBinaryHandle* h = (NoopBinaryHandle*) handle;
    free(h->buffer);
    h->buffer = nullptr;
    h->size = 0;
    h->capacity = 0;
    h->position = 0;
    h->writable = true;
    h->dirty = true; // matches the native runner, which reopens "wb+" and so always touches the file
}

static bool noopDirectoryExists(FileSystem* fs, const char* relativePath) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    return shgeti(nfs->directories, relativePath) >= 0;
}

static bool noopCreateDirectory(FileSystem* fs, const char* relativePath) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    ptrdiff_t idx = shgeti(nfs->directories, relativePath);
    if (idx >= 0) {
        return false;
    }
    shput(nfs->directories, relativePath, true);
    return true;
}

static bool noopDeleteDirectory(FileSystem* fs, const char* relativePath) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    ptrdiff_t idx = shgeti(nfs->directories, relativePath);
    if (idx < 0) {
        return false;
    }
    shdel(nfs->directories, relativePath);
    return true;
}

// ===[ Directory Enumeration ]===

// Returns true if "key" lives directly inside directory "dir" (no nested subpath).
// On match, *outBase points at the basename within key.
// "dir" uses '/' separators with no trailing slash ("" = root).
static bool keyInDir(const char* key, const char* dir, const char** outBase) {
    const char* lastSlash = strrchr(key, '/');
    size_t dirLen = strlen(dir);
    if (lastSlash == nullptr) {
        if (dirLen != 0) return false; // key is at root, but a subdir was requested
        *outBase = key;
        return true;
    }
    size_t keyDirLen = (size_t) (lastSlash - key);
    if (keyDirLen != dirLen || strncmp(key, dir, dirLen) != 0) return false;
    *outBase = lastSlash + 1;
    return true;
}

static FileSystemDirEntry* noopListDirectory(FileSystem* fs, const char* relativeDirPath) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;

    // Normalize the requested directory: '\' -> '/', strip a single trailing slash.
    char* dir = safeStrdup(relativeDirPath != nullptr ? relativeDirPath : "");
    for (int i = 0; dir[i] != '\0'; i++) {
        if (dir[i] == '\\') dir[i] = '/';
    }
    size_t dirLen = strlen(dir);
    if (dirLen > 0 && dir[dirLen - 1] == '/') dir[dirLen - 1] = '\0';

    // stb_ds dynamic array; caller releases it with arrfree() (see file_system.h).
    FileSystemDirEntry* entries = nullptr;
    const char* base;
    repeat(shlen(nfs->files), i) {
        if (keyInDir(nfs->files[i].key, dir, &base)) {
            FileSystemDirEntry e = {0};
            e.name = safeStrdup(base);
            e.isDirectory = false;
            arrput(entries, e);
        }
    }
    repeat(shlen(nfs->binaryFiles), i) {
        if (keyInDir(nfs->binaryFiles[i].key, dir, &base)) {
            FileSystemDirEntry e = {0};
            e.name = safeStrdup(base);
            e.isDirectory = false;
            arrput(entries, e);
        }
    }
    repeat(shlen(nfs->directories), i) {
        if (keyInDir(nfs->directories[i].key, dir, &base)) {
            FileSystemDirEntry e = {0};
            e.name = safeStrdup(base);
            e.isDirectory = true;
            arrput(entries, e);
        }
    }

    free(dir);
    return entries;
}

// ===[ Vtable ]===

static FileSystemVtable noopFileSystemVtable;

// ===[ Lifecycle ]===

FileSystem* NoopFileSystem_create(void) {
    NoopFileSystem* nfs = (NoopFileSystem *)safeCalloc(1, sizeof(NoopFileSystem));
    nfs->base.vtable = &noopFileSystemVtable;
    noopFileSystemVtable.resolvePath = noopResolvePath;
    noopFileSystemVtable.fileExists = noopFileExists;
    noopFileSystemVtable.readFileText = noopReadFileText;
    noopFileSystemVtable.writeFileText = noopWriteFileText;
    noopFileSystemVtable.deleteFile = noopDeleteFile;
    noopFileSystemVtable.readFileBinary = noopReadFileBinary;
    noopFileSystemVtable.writeFileBinary = noopWriteFileBinary;
    noopFileSystemVtable.binaryOpen = noopBinaryOpen;
    noopFileSystemVtable.binaryClose = noopBinaryClose;
    noopFileSystemVtable.binaryRead = noopBinaryRead;
    noopFileSystemVtable.binaryWrite = noopBinaryWrite;
    noopFileSystemVtable.binaryTell = noopBinaryTell;
    noopFileSystemVtable.binarySeek = noopBinarySeek;
    noopFileSystemVtable.binarySize = noopBinarySize;
    noopFileSystemVtable.binaryRewrite = noopBinaryRewrite;
    noopFileSystemVtable.directoryExists = noopDirectoryExists;
    noopFileSystemVtable.createDirectory = noopCreateDirectory;
    noopFileSystemVtable.deleteDirectory = noopDeleteDirectory;
    noopFileSystemVtable.listDirectory = noopListDirectory;
    nfs->files = nullptr;
    sh_new_strdup(nfs->files);
    nfs->binaryFiles = nullptr;
    sh_new_strdup(nfs->binaryFiles);
    nfs->directories = nullptr;
    sh_new_strdup(nfs->directories);
    return (FileSystem*) nfs;
}

void NoopFileSystem_destroy(FileSystem* fs) {
    NoopFileSystem* nfs = (NoopFileSystem*) fs;
    repeat(shlen(nfs->files), i) {
        free(nfs->files[i].value);
    }
    shfree(nfs->files);
    repeat(shlen(nfs->binaryFiles), i) {
        free(nfs->binaryFiles[i].value.data);
    }
    shfree(nfs->binaryFiles);
    shfree(nfs->directories);
    free(nfs);
}
