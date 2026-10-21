#include "native.h"

#include <bzlib.h>
#include <limits.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>

static size_t pixel_count(int width, int height) {
    if (width <= 0 || height <= 0 ||
        (uintmax_t)width > (uintmax_t)INT32_MAX / (uintmax_t)height ||
        (uintmax_t)width > (uintmax_t)SIZE_MAX / (uintmax_t)height)
        fail("Image: invalid or unrepresentable dimensions %d x %d", width, height);
    return (size_t)width * (size_t)height;
}

Image new_image(int width, int height) {
    size_t count = pixel_count(width, height);
    Image result = {width, height, allocate(count, sizeof(uint32_t))};
    memset(result.pixels, 0, count * sizeof(uint32_t));
    return result;
}

void free_image(Image *image) {
    if (!image) return;
    free(image->pixels);
    *image = (Image){0};
}

static int signed_bits(unsigned value, unsigned bits) {
    unsigned sign = 1u << (bits - 1);
    return (int)(value ^ sign) - (int)sign;
}

static unsigned qoi_byte(Slice payload, size_t *position) {
    if (*position >= payload.size) fail("QOI: truncated pixel opcode");
    return payload.data[(*position)++];
}

static Image decode_qoi(Slice data) {
    if (data.size < 12 || memcmp(data.data, "fioq", 4))
        fail("QOI: invalid header");
    int width = u16(data, 4), height = u16(data, 6);
    size_t count = pixel_count(width, height);
    uint32_t length = u32(data, 8);
    if ((uintmax_t)length > (uintmax_t)(data.size - 12))
        fail("QOI: truncated payload");
    Slice payload = {data.data + 12, length};
    Image image = new_image(width, height);
    uint32_t index[64] = {0};
    unsigned r = 0, g = 0, b = 0, a = 255;
    size_t pos = 0, run = 0;
    for (size_t i = 0; i < count; ++i) {
        if (run) {
            --run;
        } else {
            unsigned first = qoi_byte(payload, &pos);
            if ((first & 0xc0) == 0) {
                uint32_t color = index[first & 63];
                a = color >> 24; r = (color >> 16) & 255;
                g = (color >> 8) & 255; b = color & 255;
            } else if ((first & 0xe0) == 0x40) {
                run = first & 31;
            } else if ((first & 0xe0) == 0x60) {
                run = (((first & 31) << 8) | qoi_byte(payload, &pos)) + 32;
            } else if ((first & 0xc0) == 0x80) {
                r = (r + signed_bits((first >> 4) & 3, 2)) & 255;
                g = (g + signed_bits((first >> 2) & 3, 2)) & 255;
                b = (b + signed_bits(first & 3, 2)) & 255;
            } else if ((first & 0xe0) == 0xc0) {
                unsigned value = (first << 8) | qoi_byte(payload, &pos);
                r = (r + signed_bits((value >> 8) & 31, 5)) & 255;
                g = (g + signed_bits((value >> 4) & 15, 4)) & 255;
                b = (b + signed_bits(value & 15, 4)) & 255;
            } else if ((first & 0xf0) == 0xe0) {
                unsigned second = qoi_byte(payload, &pos);
                unsigned third = qoi_byte(payload, &pos);
                unsigned value = (first << 16) | (second << 8) | third;
                r = (r + signed_bits((value >> 15) & 31, 5)) & 255;
                g = (g + signed_bits((value >> 10) & 31, 5)) & 255;
                b = (b + signed_bits((value >> 5) & 31, 5)) & 255;
                a = (a + signed_bits(value & 31, 5)) & 255;
            } else {
                if (first & 8) r = qoi_byte(payload, &pos);
                if (first & 4) g = qoi_byte(payload, &pos);
                if (first & 2) b = qoi_byte(payload, &pos);
                if (first & 1) a = qoi_byte(payload, &pos);
            }
            if (run > count - i - 1) fail("QOI: run exceeds image dimensions");
            index[(r ^ g ^ b ^ a) & 63] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        image.pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    return image;
}

static Image decode_bz2_qoi(Slice data, bool modern) {
    size_t header = modern ? 12 : 8;
    if (data.size < header) fail("BZ2-QOI: truncated header");
    int width = u16(data, 4), height = u16(data, 6);
    pixel_count(width, height);
    size_t declared = modern ? u32(data, 8) : 0;
    if (modern && (declared < 12 || (uintmax_t)declared > UINT_MAX))
        fail("BZ2-QOI: invalid uncompressed length");
    if (data.size - header > UINT_MAX)
        fail("BZ2-QOI: compressed input too large");
    bz_stream stream = {0};
    int status = BZ2_bzDecompressInit(&stream, 0, 0);
    if (status != BZ_OK) fail("BZ2-QOI: cannot initialize decoder (%d)", status);
    stream.next_in = (char *)(data.data + header);
    stream.avail_in = (unsigned)(data.size - header);
    size_t output_limit = modern ? declared : UINT_MAX;
    size_t capacity = output_limit < 65536 ? output_limit : 65536;
    uint8_t *output = allocate(capacity, 1);
    size_t used = 0;
    for (;;) {
        if (used == capacity && capacity < output_limit) {
            size_t next = capacity > output_limit / 2 ? output_limit : capacity * 2;
            output = resize_array(output, next, 1);
            capacity = next;
        }
        /* Allow the decoder to consume its trailer at the exact output limit. */
        char overflow;
        bool at_limit = used == output_limit;
        stream.next_out = at_limit ? &overflow : (char *)output + used;
        stream.avail_out = at_limit ? 1 : (unsigned)(capacity - used);
        unsigned before_in = stream.avail_in, before_out = stream.avail_out;
        status = BZ2_bzDecompress(&stream);
        unsigned produced = before_out - stream.avail_out;
        if (at_limit && produced) fail("BZ2-QOI: decompressed data exceeds length limit");
        used += produced;
        if (status == BZ_STREAM_END) break;
        if (status != BZ_OK) fail("BZ2-QOI: invalid compressed stream (%d)", status);
        if (before_in == stream.avail_in && before_out == stream.avail_out)
            fail("BZ2-QOI: truncated compressed stream");
    }
    BZ2_bzDecompressEnd(&stream);
    if (modern && used != declared) fail("BZ2-QOI: uncompressed length mismatch");
    Image result = decode_qoi((Slice){output, used});
    free(output);
    if (result.width != width || result.height != height)
        fail("BZ2-QOI: outer and inner dimensions disagree");
    return result;
}

#if PNG_LIBPNG_VER_MAJOR <= 1 && PNG_LIBPNG_VER_MINOR < 6
struct PngMemorySource {
    const uint8_t *data;
    size_t size;
    size_t offset;
};

static void png_read_memory_callback(png_structp png_ptr, png_bytep out, png_size_t size) {
    struct PngMemorySource *src = (struct PngMemorySource *)png_get_io_ptr(png_ptr);
    if (size > src->size - src->offset) size = src->size - src->offset;
    if (size > 0) {
        memcpy(out, src->data + src->offset, size);
        src->offset += size;
    }
}
#endif

Image decode_image(Slice data, bool gm2022_5) {
    if (!data.data || data.size < 4) fail("Image: truncated header");
    if (!memcmp(data.data, "fioq", 4)) return decode_qoi(data);
    if (!memcmp(data.data, "2zoq", 4)) return decode_bz2_qoi(data, gm2022_5);
    if (data.size < 8 || png_sig_cmp((png_bytep)data.data, 0, 8))
        fail("Image: unsupported format (expected PNG, fioq or 2zoq)");
#if PNG_LIBPNG_VER_MAJOR > 1 || (PNG_LIBPNG_VER_MAJOR == 1 && PNG_LIBPNG_VER_MINOR >= 6)
    png_image png = {0};
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, data.data, data.size))
        fail("PNG: %s", png.message);
    if (png.width > INT_MAX || png.height > INT_MAX) fail("PNG: dimensions too large");
    Image result = new_image((int)png.width, (int)png.height);
    size_t count = pixel_count(result.width, result.height);
    uint8_t *rgba = allocate(count, 4);
    png.format = PNG_FORMAT_RGBA;
    if (!png_image_finish_read(&png, NULL, rgba, 0, NULL))
        fail("PNG: %s", png.message);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *p = rgba + i * 4;
        result.pixels[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
                           ((uint32_t)p[1] << 8) | p[2];
    }
    png_image_free(&png);
    free(rgba);
    return result;
#else
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) fail("PNG: cannot create read struct");
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fail("PNG: cannot create info struct");
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fail("PNG: decode failed");
    }
    struct PngMemorySource source = { data.data, data.size, 0 };
    png_set_read_fn(png, &source, png_read_memory_callback);
    png_read_info(png, info);
    png_uint_32 width = 0, height = 0;
    int bit_depth = 0, color_type = 0, interlace_type = 0, compression_type = 0, filter_method = 0;
    png_get_IHDR(png, info, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);
    if (width > INT_MAX || height > INT_MAX) fail("PNG: dimensions too large");
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (bit_depth < 8) png_set_packing(png);
    png_read_update_info(png, info);
    Image result = new_image((int)width, (int)height);
    size_t rowbytes = png_get_rowbytes(png, info);
    png_bytep row = (png_bytep)allocate(rowbytes, 1);
    for (uint32_t y = 0; y < height; ++y) {
        png_read_row(png, row, NULL);
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *p = row + x * 4;
            size_t index = (size_t)y * width + x;
            result.pixels[index] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
                                   ((uint32_t)p[1] << 8) | p[2];
        }
    }
    free(row);
    png_destroy_read_struct(&png, &info, NULL);
    return result;
#endif
}

void save_png(const char *path, const Image *image) {
    size_t count = pixel_count(image->width, image->height);
    if (!image->pixels) fail("PNG: missing pixel data");
    uint8_t *rgba = allocate(count, 4);
    for (size_t i = 0; i < count; ++i) {
        uint32_t p = image->pixels[i];
        rgba[i * 4] = (uint8_t)(p >> 16);
        rgba[i * 4 + 1] = (uint8_t)(p >> 8);
        rgba[i * 4 + 2] = (uint8_t)p;
        rgba[i * 4 + 3] = (uint8_t)(p >> 24);
    }
#if PNG_LIBPNG_VER_MAJOR > 1 || (PNG_LIBPNG_VER_MAJOR == 1 && PNG_LIBPNG_VER_MINOR >= 6)
    png_image png = {0};
    png.version = PNG_IMAGE_VERSION;
    png.width = (png_uint_32)image->width;
    png.height = (png_uint_32)image->height;
    png.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&png, path, 0, rgba, 0, NULL))
        fail("PNG: cannot write %s: %s", path, png.message);
    png_image_free(&png);
#else
    FILE *fp = fopen(path, "wb");
    if (!fp) fail("PNG: cannot write %s", path);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        fail("PNG: cannot create write struct");
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        fail("PNG: cannot create info struct");
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        fail("PNG: cannot write %s", path);
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, image->width, image->height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < image->height; ++y) {
        png_write_row(png, (png_bytep)(rgba + (size_t)y * image->width * 4));
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
#endif
    free(rgba);
}

static int compare_color(const void *a, const void *b) {
    uint32_t av = *(const uint32_t *)a, bv = *(const uint32_t *)b;
    return (av > bv) - (av < bv);
}

static int color_index(const uint32_t *colors, int count, uint32_t color) {
    int low = 0, high = count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (colors[middle] < color) low = middle + 1;
        else high = middle;
    }
    return low < count && colors[low] == color ? low : -1;
}

typedef struct {
    size_t offset, count;
    unsigned range;
    unsigned shift;
} ColorBox;

static ColorBox color_box(const uint32_t *pixels, size_t offset, size_t count) {
    unsigned minimum[4] = {255, 255, 255, 255}, maximum[4] = {0};
    const unsigned shifts[4] = {16, 8, 0, 24};
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < 4; ++c) {
            unsigned value = (pixels[offset + i] >> shifts[c]) & 255;
            if (minimum[c] > value) minimum[c] = value;
            if (maximum[c] < value) maximum[c] = value;
        }
    }
    ColorBox box = {offset, count, 0, 16};
    for (int c = 0; c < 4; ++c) {
        unsigned range = maximum[c] - minimum[c];
        if (range > box.range) { box.range = range; box.shift = shifts[c]; }
    }
    return box;
}

static int quantize(uint32_t *pixels, size_t count, int maximum, uint32_t *palette) {
    ColorBox boxes[256];
    int box_count = 1;
    boxes[0] = color_box(pixels, 0, count);
    uint32_t *scratch = allocate(count, sizeof(uint32_t));
    while (box_count < maximum) {
        int target = -1;
        uint64_t best = 0;
        for (int i = 0; i < box_count; ++i) {
            uint64_t score = (uint64_t)boxes[i].count * boxes[i].range;
            if (boxes[i].count > 1 && score > best) { best = score; target = i; }
        }
        if (target < 0) break;
        ColorBox box = boxes[target];
        size_t frequencies[256] = {0}, positions[256], offset = 0;
        for (size_t i = 0; i < box.count; ++i)
            ++frequencies[(pixels[box.offset + i] >> box.shift) & 255];
        for (int i = 0; i < 256; ++i) {
            positions[i] = offset;
            offset += frequencies[i];
        }
        for (size_t i = 0; i < box.count; ++i) {
            uint32_t p = pixels[box.offset + i];
            scratch[positions[(p >> box.shift) & 255]++] = p;
        }
        memcpy(pixels + box.offset, scratch, box.count * sizeof(uint32_t));
        size_t middle = box.count / 2, forward = middle, backward = middle;
        unsigned key = (scratch[middle] >> box.shift) & 255;
        while (forward < box.count && ((scratch[forward] >> box.shift) & 255) == key) ++forward;
        while (backward && ((scratch[backward - 1] >> box.shift) & 255) == key) --backward;
        size_t split;
        if (backward && forward < box.count)
            split = middle - backward <= forward - middle ? backward : forward;
        else if (backward) split = backward;
        else if (forward < box.count) split = forward;
        else { boxes[target].range = 0; continue; }
        memmove(boxes + target, boxes + target + 1,
                (size_t)(box_count - target - 1) * sizeof(ColorBox));
        --box_count;
        boxes[box_count++] = color_box(pixels, box.offset, split);
        boxes[box_count++] = color_box(pixels, box.offset + split, box.count - split);
    }
    for (int i = 0; i < box_count; ++i) {
        uint64_t sums[4] = {0};
        ColorBox box = boxes[i];
        for (size_t j = 0; j < box.count; ++j) {
            uint32_t p = pixels[box.offset + j];
            for (int c = 0; c < 4; ++c) sums[c] += (p >> (c * 8)) & 255;
        }
        palette[i] = 0;
        for (int c = 0; c < 4; ++c)
            palette[i] |= (uint32_t)(sums[c] / box.count) << (c * 8);
    }
    free(scratch);
    return box_count;
}

static int nearest_color(uint32_t color, const uint32_t *palette, int count, int start) {
    int best = start, distance = INT_MAX;
    for (int i = start; i < count; ++i) {
        int d = 0;
        for (int shift = 0; shift <= 24; shift += 8) {
            int delta = (int)((color >> shift) & 255) - (int)((palette[i] >> shift) & 255);
            d += delta * delta;
        }
        if (d < distance) { distance = d; best = i; }
    }
    return best;
}

ClutImage make_clut(const char *name, const Image *image, bool force4) {
    size_t count = pixel_count(image->width, image->height);
    if (!name || !image->pixels) fail("CLUT: missing name or pixels");
    uint32_t unique[257];
    uint32_t unique_keys[1024] = {0};
    bool unique_occupied[1024] = {0};
    int unique_count = 0;
    bool transparent = false;
    for (size_t i = 0; i < count; ++i) {
        uint32_t p = image->pixels[i] >> 24 ? image->pixels[i] : 0;
        if (!p) transparent = true;
        if (unique_count > 256) continue;
        size_t slot = (p * UINT32_C(2654435761)) >> 22;
        while (unique_occupied[slot] && unique_keys[slot] != p) slot = (slot + 1) & 1023;
        if (!unique_occupied[slot]) {
            unique_occupied[slot] = true;
            unique_keys[slot] = p;
            unique[unique_count++] = p;
        }
    }
    ClutImage result = {0};
    result.name = copy_string(name);
    result.width = image->width; result.height = image->height;
    result.bpp = force4 || unique_count <= 16 ? 4 : 8;
    result.group = -1;
    result.indices = allocate(count, 1);
    int slots = result.bpp == 4 ? 16 : 256;
    bool quantized = unique_count > slots;
    if (!quantized) {
        memcpy(result.palette, unique, (size_t)unique_count * sizeof(uint32_t));
        result.used = unique_count;
    } else {
        uint32_t *visible = allocate(count, sizeof(uint32_t));
        size_t visible_count = 0;
        for (size_t i = 0; i < count; ++i)
            if (image->pixels[i] >> 24) visible[visible_count++] = image->pixels[i];
        result.used = transparent ? 1 : 0;
        if (visible_count)
            result.used += quantize(visible, visible_count, slots - result.used,
                                    result.palette + result.used);
        free(visible);
    }
    qsort(result.palette, (size_t)result.used, sizeof(uint32_t), compare_color);
    typedef struct { uint32_t color; int index_plus_one; } Cache;
    Cache *cache = quantized ? allocate(4096, sizeof(Cache)) : NULL;
    if (cache) memset(cache, 0, 4096 * sizeof(Cache));
    for (size_t i = 0; i < count; ++i) {
        uint32_t p = image->pixels[i] >> 24 ? image->pixels[i] : 0;
        int index;
        if (!quantized) index = color_index(result.palette, result.used, p);
        else if (!p) index = 0;
        else {
            Cache *entry = &cache[((p * UINT32_C(2654435761)) >> 20) & 4095];
            if (entry->index_plus_one && entry->color == p) index = entry->index_plus_one - 1;
            else {
                index = nearest_color(p, result.palette, result.used, transparent ? 1 : 0);
                *entry = (Cache){p, index + 1};
            }
        }
        if (index < 0) fail("CLUT: internal palette mapping failure");
        result.indices[i] = (uint8_t)index;
    }
    free(cache);
    return result;
}

void free_clut(ClutImage *image) {
    if (!image) return;
    free(image->name);
    free(image->indices);
    *image = (ClutImage){0};
}

static void validate_clut(const ClutImage *image) {
    size_t count = pixel_count(image->width, image->height);
    int slots = image->bpp == 4 ? 16 : 256;
    if ((image->bpp != 4 && image->bpp != 8) ||
        image->used <= 0 || image->used > slots || !image->indices)
        fail("CLUT: invalid palette image");
    for (int i = 1; i < image->used; ++i)
        if (image->palette[i - 1] > image->palette[i]) fail("CLUT: palette is not sorted");
    for (size_t i = 0; i < count; ++i)
        if (image->indices[i] >= image->used) fail("CLUT: pixel index outside palette");
}

static int palette_union(const ClutGroup *a, const ClutGroup *b, uint32_t *out) {
    int ai = 0, bi = 0, n = 0;
    while (ai < a->used || bi < b->used) {
        uint32_t value;
        if (bi == b->used || (ai < a->used && a->palette[ai] < b->palette[bi]))
            value = a->palette[ai++];
        else if (ai == a->used || b->palette[bi] < a->palette[ai])
            value = b->palette[bi++];
        else { value = a->palette[ai++]; ++bi; }
        if (!n || out[n - 1] != value) out[n++] = value;
    }
    return n;
}

static void remap_clut(ClutImage *image, const ClutGroup *group) {
    uint8_t remap[256];
    for (int i = 0; i < image->used; ++i) {
        int index = color_index(group->palette, group->used, image->palette[i]);
        if (index < 0) fail("CLUT: merged palette lost a color");
        remap[i] = (uint8_t)index;
    }
    size_t count = pixel_count(image->width, image->height);
    for (size_t i = 0; i < count; ++i) image->indices[i] = remap[image->indices[i]];
    memcpy(image->palette, group->palette, sizeof(image->palette));
    image->used = group->used;
    image->group = group->id;
}

Groups merge_cluts(ClutImage *images, size_t count) {
    Groups result = {0};
    if (!count) return result;
    if (!images || count > INT_MAX || count > SIZE_MAX / 2)
        fail("CLUT: excessive image count");
    ClutGroup *groups = allocate(count, sizeof(ClutGroup));
    bool *alive = allocate(count, sizeof(bool));
    size_t *heads = allocate(count, sizeof(size_t));
    size_t *tails = allocate(count, sizeof(size_t));
    size_t *next_image = allocate(count, sizeof(size_t));
    size_t table_size = 1;
    while (table_size < count * 2) {
        if (table_size > SIZE_MAX / 2) fail("CLUT: palette lookup size overflow");
        table_size *= 2;
    }
    size_t *table = allocate(table_size, sizeof(size_t));
    memset(table, 0, table_size * sizeof(size_t));
    size_t group_count = 0;
    for (size_t i = 0; i < count; ++i) {
        validate_clut(&images[i]);
        uint32_t hash = (uint32_t)images[i].used;
        for (int j = 0; j < images[i].used; ++j) hash = hash * 31 + images[i].palette[j];
        size_t slot = hash & (table_size - 1);
        size_t found = group_count;
        while (table[slot]) {
            size_t j = table[slot] - 1;
            if (images[j].used == images[i].used &&
                !memcmp(images[j].palette, images[i].palette,
                        (size_t)images[i].used * sizeof(uint32_t))) {
                found = (size_t)images[j].group;
                break;
            }
            slot = (slot + 1) & (table_size - 1);
        }
        if (found == group_count) {
            table[slot] = i + 1;
            ClutGroup *g = &groups[group_count];
            *g = (ClutGroup){.id = (int)group_count, .bpp = images[i].bpp};
            for (int j = 0; j < images[i].used; ++j)
                if (!g->used || g->palette[g->used - 1] != images[i].palette[j])
                    g->palette[g->used++] = images[i].palette[j];
            heads[group_count] = tails[group_count] = i;
            alive[group_count++] = true;
        } else {
            next_image[tails[found]] = i;
            tails[found] = i;
        }
        next_image[i] = SIZE_MAX;
        images[i].group = (int)found;
    }
    free(table);
    size_t *order = allocate(group_count, sizeof(size_t));
    uint32_t combined[512];
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        int slots = bpp == 4 ? 16 : 256;
        bool changed;
        do {
            changed = false;
            size_t n = 0;
            for (size_t i = 0; i < group_count; ++i) {
                if (!alive[i] || groups[i].bpp != bpp) continue;
                size_t at = n;
                while (at && groups[order[at - 1]].used > groups[i].used) {
                    order[at] = order[at - 1]; --at;
                }
                order[at] = i; ++n;
            }
            for (size_t i = 0; i < n; ++i) {
                size_t a = order[i], partner = SIZE_MAX;
                if (!alive[a] || groups[a].used >= slots) continue;
                int best_size = INT_MAX;
                for (size_t j = 0; j < n; ++j) {
                    size_t b = order[j];
                    if (a == b || !alive[b]) continue;
                    int used = palette_union(&groups[a], &groups[b], combined);
                    if (used <= slots && used < best_size) {
                        best_size = used; partner = b;
                        int larger = groups[a].used > groups[b].used ? groups[a].used : groups[b].used;
                        if (used == larger) break;
                    }
                }
                if (partner == SIZE_MAX) continue;
                ClutGroup *target = &groups[partner];
                int used = palette_union(&groups[a], target, combined);
                memset(target->palette, 0, sizeof(target->palette));
                memcpy(target->palette, combined, (size_t)used * sizeof(uint32_t));
                target->used = used;
                for (size_t j = heads[partner]; j != SIZE_MAX; j = next_image[j])
                    remap_clut(&images[j], target);
                for (size_t j = heads[a]; j != SIZE_MAX; j = next_image[j])
                    remap_clut(&images[j], target);
                next_image[tails[partner]] = heads[a];
                tails[partner] = tails[a];
                alive[a] = false;
                changed = true;
            }
        } while (changed);
    }
    result.items = allocate(group_count, sizeof(ClutGroup));
    for (size_t i = 0; i < group_count; ++i)
        if (alive[i]) result.items[result.count++] = groups[i];
    for (size_t i = 0; i < count; ++i)
        remap_clut(&images[i], &groups[images[i].group]);
    free(order);
    free(heads);
    free(tails);
    free(next_image);
    free(alive);
    free(groups);
    return result;
}

typedef struct { int x, y, width, height; } Rect;
typedef struct { Rect *items; size_t count, capacity; } Packer;

static void add_rect(Packer *packer, Rect rect) {
    if (packer->count == packer->capacity) {
        if (packer->capacity > SIZE_MAX / 2) fail("Atlas: free rectangle count overflow");
        size_t capacity = packer->capacity ? packer->capacity * 2 : 16;
        packer->items = resize_array(packer->items, capacity, sizeof(Rect));
        packer->capacity = capacity;
    }
    packer->items[packer->count++] = rect;
}

static Packer clone_packer(const Packer *source) {
    Packer copy = {0};
    if (source->count) {
        copy.items = allocate(source->count, sizeof(Rect));
        memcpy(copy.items, source->items, source->count * sizeof(Rect));
        copy.count = copy.capacity = source->count;
    }
    return copy;
}

static bool contains_rect(Rect a, Rect b) {
    return a.x <= b.x && a.y <= b.y &&
           a.x + a.width >= b.x + b.width && a.y + a.height >= b.y + b.height;
}

static bool insert_rect(Packer *packer, int width, int height, int *x, int *y) {
    int best_short = INT_MAX, best_long = INT_MAX;
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < packer->count; ++i) {
        Rect r = packer->items[i];
        if (width > r.width || height > r.height) continue;
        int dw = r.width - width, dh = r.height - height;
        int short_side = dw < dh ? dw : dh, long_side = dw > dh ? dw : dh;
        if (short_side < best_short || (short_side == best_short && long_side < best_long)) {
            best = i; best_short = short_side; best_long = long_side;
        }
    }
    if (best == SIZE_MAX) return false;
    Rect used = {packer->items[best].x, packer->items[best].y, width, height};
    *x = used.x; *y = used.y;
    Packer strips = {0};
    size_t write = 0, original_count = packer->count;
    for (size_t i = 0; i < original_count; ++i) {
        Rect r = packer->items[i];
        if (r.x >= used.x + width || r.x + r.width <= used.x ||
            r.y >= used.y + height || r.y + r.height <= used.y) {
            packer->items[write++] = r;
            continue;
        }
        if (r.x < used.x) add_rect(&strips, (Rect){r.x, r.y, used.x - r.x, r.height});
        if (r.x + r.width > used.x + width)
            add_rect(&strips, (Rect){used.x + width, r.y, r.x + r.width - used.x - width, r.height});
        if (r.y < used.y) add_rect(&strips, (Rect){r.x, r.y, r.width, used.y - r.y});
        if (r.y + r.height > used.y + height)
            add_rect(&strips, (Rect){r.x, used.y + height, r.width, r.y + r.height - used.y - height});
    }
    packer->count = write;
    for (size_t i = 0; i < strips.count; ++i) add_rect(packer, strips.items[i]);
    free(strips.items);
    /* Preserve Kotlin's in-place pruning order, including its tie behavior. */
    size_t size = packer->count;
    write = 0;
    for (size_t i = 0; i < size; ++i) {
        bool contained = false;
        Rect r = packer->items[i];
        for (size_t j = 0; j < size; ++j)
            if (i != j && contains_rect(packer->items[j], r)) { contained = true; break; }
        if (!contained) packer->items[write++] = r;
    }
    packer->count = write;
    return true;
}

typedef struct {
    const char *key;
    size_t *images, count;
    uint64_t area;
} PackGroup;

static int longest_side(const ClutImage *image) {
    return image->width > image->height ? image->width : image->height;
}

static void add_placement(Atlas *atlas, Placement placement) {
    atlas->entries = resize_array(atlas->entries, atlas->count + 1, sizeof(Placement));
    atlas->entries[atlas->count++] = placement;
}

Atlases pack_atlases(const ClutImage *images, const char *const *keys, size_t count, int size) {
    Atlases result = {0};
    pixel_count(size, size);
    if (count > INT_MAX || (count && !images))
        fail("Atlas: excessive or invalid image count");
    for (size_t i = 0; i < count; ++i) {
        pixel_count(images[i].width, images[i].height);
        if ((images[i].bpp != 4 && images[i].bpp != 8) ||
            images[i].width > size || images[i].height > size)
            fail("Atlas: image %s does not fit %d x %d atlas",
                 images[i].name ? images[i].name : "(unnamed)", size, size);
    }
    Packer *packers = NULL;
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        PackGroup *groups = allocate(count, sizeof(PackGroup));
        size_t group_count = 0;
        for (size_t i = 0; i < count; ++i) {
            if (images[i].bpp != bpp) continue;
            const char *key = keys && keys[i] ? keys[i] : images[i].name;
            if (!key) fail("Atlas: missing grouping key");
            size_t g = 0;
            while (g < group_count && strcmp(groups[g].key, key)) ++g;
            if (g == group_count) {
                groups[g] = (PackGroup){.key = key};
                ++group_count;
            }
            PackGroup *group = &groups[g];
            group->images = resize_array(group->images, group->count + 1, sizeof(size_t));
            group->images[group->count++] = i;
            group->area += (uint64_t)images[i].width * (unsigned)images[i].height;
        }
        for (size_t i = 1; i < group_count; ++i) {
            PackGroup group = groups[i];
            size_t j = i;
            while (j && groups[j - 1].area < group.area) {
                groups[j] = groups[j - 1]; --j;
            }
            groups[j] = group;
        }
        for (size_t g = 0; g < group_count; ++g) {
            PackGroup *group = &groups[g];
            for (size_t i = 1; i < group->count; ++i) {
                size_t image = group->images[i], j = i;
                while (j && longest_side(&images[group->images[j - 1]]) < longest_side(&images[image])) {
                    group->images[j] = group->images[j - 1]; --j;
                }
                group->images[j] = image;
            }
            Placement *placements = allocate(group->count, sizeof(Placement));
            bool packed = false;
            for (size_t i = 0; i < result.count; ++i) {
                if (result.items[i].bpp != bpp) continue;
                Packer trial = clone_packer(&packers[i]);
                size_t j;
                for (j = 0; j < group->count; ++j) {
                    size_t image = group->images[j];
                    placements[j].image = image;
                    if (!insert_rect(&trial, images[image].width, images[image].height,
                                     &placements[j].x, &placements[j].y)) break;
                }
                if (j == group->count) {
                    free(packers[i].items);
                    packers[i] = trial;
                    for (j = 0; j < group->count; ++j) add_placement(&result.items[i], placements[j]);
                    packed = true;
                    break;
                }
                free(trial.items);
            }
            if (!packed) {
                size_t remaining = group->count;
                while (remaining) {
                    if (result.count >= INT_MAX) fail("Atlas: excessive atlas count");
                    Atlas atlas = {.id = (int)result.count, .bpp = bpp, .width = size, .height = size};
                    Packer packer = {0};
                    add_rect(&packer, (Rect){0, 0, size, size});
                    size_t write = 0;
                    for (size_t i = 0; i < remaining; ++i) {
                        size_t image = group->images[i];
                        Placement placement = {.image = image};
                        if (insert_rect(&packer, images[image].width, images[image].height,
                                        &placement.x, &placement.y))
                            add_placement(&atlas, placement);
                        else group->images[write++] = image;
                    }
                    if (!atlas.count) fail("Atlas: could not pack remaining images");
                    result.items = resize_array(result.items, result.count + 1, sizeof(Atlas));
                    packers = resize_array(packers, result.count + 1, sizeof(Packer));
                    result.items[result.count] = atlas;
                    packers[result.count++] = packer;
                    remaining = write;
                }
            }
            free(placements);
            free(group->images);
        }
        free(groups);
    }
    for (size_t i = 0; i < result.count; ++i) free(packers[i].items);
    free(packers);
    return result;
}

void free_atlases(Atlases *atlases) {
    if (!atlases) return;
    for (size_t i = 0; i < atlases->count; ++i) free(atlases->items[i].entries);
    free(atlases->items);
    *atlases = (Atlases){0};
}
