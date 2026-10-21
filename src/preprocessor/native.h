#ifndef BUTTERSCOTCH_NATIVE_H
#define BUTTERSCOTCH_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *data; size_t size, capacity; } Buffer;
typedef struct { const uint8_t *data; size_t size; } Slice;
_Noreturn void fail(const char *format, ...);
void *allocate(size_t count, size_t size);
void *resize_array(void *ptr, size_t count, size_t size);
char *copy_string(const char *text);
char *format_string(const char *format, ...);
Buffer read_file(const char *path);
void write_file(const char *path, const void *data, size_t size);
void mkdirs(const char *path);
char *join_path(const char *dir, const char *name);
void append(Buffer *out, const void *data, size_t size);
void put8(Buffer *out, unsigned value);
void put16(Buffer *out, uint64_t value, bool big);
void put32(Buffer *out, uint64_t value, bool big);
void put64(Buffer *out, uint64_t value);
uint8_t u8(Slice s, size_t at);
uint16_t u16(Slice s, size_t at);
uint32_t u32(Slice s, size_t at);
int32_t i32(Slice s, size_t at);
float f32(Slice s, size_t at);
Slice section(Slice s, size_t at, size_t size);

typedef struct { int width, height; uint32_t *pixels; } Image;
Image decode_image(Slice data, bool gm2022_5);
void save_png(const char *path, const Image *image);
void free_image(Image *image);
Image new_image(int width, int height);

typedef struct {
    char *name;
    int width, height, bpp, used, group;
    uint32_t palette[256];
    uint8_t *indices;
} ClutImage;
typedef struct { int id, bpp, used; uint32_t palette[256]; } ClutGroup;
typedef struct { ClutGroup *items; size_t count; } Groups;
ClutImage make_clut(const char *name, const Image *image, bool force4);
Groups merge_cluts(ClutImage *images, size_t count);
void free_clut(ClutImage *image);
typedef struct { size_t image; int x, y; } Placement;
typedef struct {
    int id, bpp, width, height;
    Placement *entries;
    size_t count;
} Atlas;
typedef struct { Atlas *items; size_t count; } Atlases;
Atlases pack_atlases(const ClutImage *images, const char *const *keys,
                    size_t count, int size);
void free_atlases(Atlases *atlases);

typedef struct {
    uint32_t offset;
    int sx, sy, sw, sh, tx, ty, tw, th, bw, bh, page;
} Tpag;
typedef struct { char *name; uint32_t *textures; size_t count; } Sprite;
typedef struct { char *name; uint32_t texture; } NamedTexture;
typedef struct { bool sprite; int definition, x, y, width, height; } Tile;
typedef struct {
    char *name, *file;
    uint32_t flags;
    float volume, pitch;
    int group, audio;
} Sound;
typedef struct {
    Buffer source;
    int version[4], bytecode;
    Tpag *tpags; size_t tpag_count;
    Slice *textures; size_t texture_count;
    Sprite *sprites; size_t sprite_count;
    NamedTexture *backgrounds; size_t background_count;
    NamedTexture *fonts; size_t font_count;
    Tile *tiles; size_t tile_count;
    Sound *sounds; size_t sound_count;
    Slice *audio; size_t audio_count;
} DataWin;
DataWin parse_datawin(Buffer source, bool audio_only, bool ps3);
void free_datawin(DataWin *dw);
bool version_at_least(const DataWin *dw, int major, int minor, int release, int build);
int resolve_tpag(const DataWin *dw, uint32_t offset);
Image extract_tpag(const Tpag *tpag, const Image *pages, size_t page_count);
void process_audio(const DataWin *dw, const char *input_directory,
                   Buffer *sound_bank, Buffer *sounds);

typedef struct {
    const char *input, *output, *debug;
    bool ps3;
    int atlas_size;
    char **patterns;
    size_t pattern_count;
} Options;
void process(const Options *options);

#endif
