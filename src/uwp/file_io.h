#ifndef BS_UWP_FILE_IO_H
#define BS_UWP_FILE_IO_H

#include <stdio.h>
#include <wchar.h>

FILE* uwpFopen(const char* path, const char* mode);
FILE* uwpWFopen(const wchar_t* path, const wchar_t* mode);

#endif
