#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#elif defined(__clang__)
#pragma clang diagnostic ignored "-Wuninitialized"
#endif
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
