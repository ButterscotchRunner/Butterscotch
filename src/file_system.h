#pragma once

#include "common.h"
#include <stdint.h>
// ===[ FileSystem Vtable ]===
// Platform-agnostic file system interface

typedef struct FileSystem FileSystem;

// Mode values for FileSystemVtable.binaryOpen
#define GML_FILE_BIN_READ 0
#define GML_FILE_BIN_WRITE 1 // truncates if the file exists, creates it if not
#define GML_FILE_BIN_READWRITE 2 // creates the file if missing, preserves contents otherwise

typedef struct {
    // Resolve a game-relative path to a full platform path (caller frees result)
    char* (*resolvePath)(FileSystem* fs, const char* relativePath);
    // Check if a file exists
    bool (*fileExists)(FileSystem* fs, const char* relativePath);
    // Read entire file contents into a string (caller frees result), returns nullptr if not found
    char* (*readFileText)(FileSystem* fs, const char* relativePath);
    // Write string contents to a file (creates/overwrites), returns true on success
    bool (*writeFileText)(FileSystem* fs, const char* relativePath, const char* contents);
    // Delete a file, returns true on success
    bool (*deleteFile)(FileSystem* fs, const char* relativePath);
    // Read entire file as binary data (caller frees *outData), returns true on success
    bool (*readFileBinary)(FileSystem* fs, const char* relativePath, uint8_t** outData, int32_t* outSize);
    // Write binary data to a file (creates/overwrites), returns true on success
    bool (*writeFileBinary)(FileSystem* fs, const char* relativePath, const uint8_t* data, int32_t size);

    // Streaming binary access (for GML file_bin_*).
    // The implementation chooses what "handle" means, callers only ever pass it back through these vtable entries.
    // mode is one of the GML_FILE_BIN_* constants above. binaryOpen returns nullptr if the file can't be opened.
    void* (*binaryOpen)(FileSystem* fs, const char* relativePath, int32_t mode);
    void (*binaryClose)(FileSystem* fs, void* handle);
    int32_t (*binaryRead)(FileSystem* fs, void* handle, void* dst, int32_t n);
    int32_t (*binaryWrite)(FileSystem* fs, void* handle, const void* src, int32_t n);
    int32_t (*binaryTell)(FileSystem* fs, void* handle);
    bool (*binarySeek)(FileSystem* fs, void* handle, int32_t pos);
    int32_t (*binarySize)(FileSystem* fs, void* handle);
    // Truncates the file to zero length and rewinds
    void (*binaryRewrite)(FileSystem* fs, void* handle);

    // Check if a directory exists
    bool (*directoryExists)(FileSystem* fs, const char* relativePath);
    // Create a directory, returns true on success
    bool (*createDirectory)(FileSystem* fs, const char* relativePath);
    // Delete a directory, returns true on success
    bool (*deleteDirectory)(FileSystem* fs, const char* relativePath);
} FileSystemVtable;

struct FileSystem {
    FileSystemVtable* vtable;
};
