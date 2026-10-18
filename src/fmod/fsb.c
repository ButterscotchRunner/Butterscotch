#include "fsb.h"
#include "fmod_reader.h"
#include "vorbis_setups.h"
#include "miniz.h"
#include "stb_ds.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// ===[ sample headers ]===

bool Fsb_parse(const uint8_t* h, size_t size, uint32_t* dataOffset, FsbSample** out) {
    static const uint32_t rates[] = {4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
    uint32_t count, headers, names, bytes, mode, i;
    size_t pos = 60, nameBase, end;
    FsbSample* samples = nullptr;
    *out = nullptr;
    if (size < 60 || memcmp(h, "FSB5", 4) || fm32(h + 4) != 1) return false;
    count = fm32(h + 8);
    headers = fm32(h + 12);
    names = fm32(h + 16);
    bytes = fm32(h + 20);
    mode = fm32(h + 24);
    if (count > 8192 || headers > size - 60 || names > size - 60 - headers || mode != 15) return false;
    nameBase = 60 + headers;
    end = nameBase + names;
    if (end > UINT32_MAX || (names && count > names / 4)) return false;
    for (i = 0; i < count; i++) {
        uint64_t bits;
        bool extra;
        FsbSample s;
        memset(&s, 0, sizeof(s));
        s.peak = 1;
        if (pos + 8 > nameBase) goto bad;
        bits = fm64(h + pos);
        pos += 8;
        extra = (bits & 1) != 0;
        s.channels = (uint32_t) ((bits >> 5) & 1) + 1;
        s.offset = (uint32_t) ((bits >> 6) & 0xfffffff) * 16;
        s.frames = (uint32_t) (bits >> 34);
        {
            unsigned r = (unsigned) ((bits >> 1) & 15);
            if (r >= sizeof(rates) / sizeof(rates[0])) goto bad;
            s.rate = rates[r];
        }
        while (extra) {
            uint32_t info, n, type;
            if (pos + 4 > nameBase) goto bad;
            info = fm32(h + pos);
            pos += 4;
            extra = (info & 1) != 0;
            n = (info >> 1) & 0xffffff;
            type = info >> 25;
            if (n > nameBase - pos) goto bad;
            if (type == 1 && n == 1) s.channels = h[pos];
            if (type == 2 && n == 4) s.rate = fm32(h + pos);
            if (type == 3 && n >= 8) {
                s.loopStart = fm32(h + pos);
                s.loopEnd = fm32(h + pos + 4) + 1;
            }
            if (type == 11 && n >= 4) s.setup = fm32(h + pos);
            if (type == 13 && n == 4) s.peak = fmfloat(h + pos);
            pos += n;
        }
        if (!s.setup || !s.channels || s.channels > 2 || !s.rate || s.offset > bytes) goto bad;
        if (names) {
            uint32_t off = fm32(h + nameBase + i * 4);
            const uint8_t* nul;
            if (off >= names || off < count * 4) goto bad;
            nul = (const uint8_t*) memchr(h + nameBase + off, 0, names - off);
            if (!nul) goto bad;
            s.name = (char*) malloc((size_t) (nul - (h + nameBase + off)) + 1);
            if (!s.name) goto bad;
            memcpy(s.name, h + nameBase + off, (size_t) (nul - (h + nameBase + off)) + 1);
        }
        if (i && s.offset < samples[i - 1].offset) {
            free(s.name);
            goto bad;
        }
        arrput(samples, s);
    }
    if (pos != nameBase) goto bad;
    for (i = 0; i < count; i++) {
        samples[i].bytes = (i + 1 < count ? samples[i + 1].offset : bytes) - samples[i].offset;
    }
    *dataOffset = (uint32_t) end;
    *out = samples;
    return true;
bad:
    Fsb_free(samples);
    return false;
}

// ===[ ogg pages ]===

// one complete packet per page. the decoder needs the granule positions for seeking.
static bool appendOggPage(uint8_t** out, const uint8_t* packet, size_t packetSize, uint64_t granule, uint32_t sequence, uint8_t flags) {
    static uint32_t table[256];
    static bool ready = false;
    size_t start = (size_t) arrlen(*out), segments = packetSize / 255 + 1, i, total = 27 + segments + packetSize;
    uint8_t* p;
    uint32_t crc = 0;
    if (segments > 255 || total > INT_MAX || start > INT_MAX - total) return false;
    arrsetlen(*out, start + total);
    p = *out + start;
    memset(p, 0, 27);
    memcpy(p, "OggS", 4);
    p[5] = flags;
    fmput32(p + 6, (uint32_t) granule);
    fmput32(p + 10, (uint32_t) (granule >> 32));
    fmput32(p + 14, 1);
    fmput32(p + 18, sequence);
    p[26] = (uint8_t) segments;
    for (i = 0; i < segments; i++) {
        p[27 + i] = (uint8_t) (i + 1 < segments ? 255 : packetSize % 255);
    }
    memcpy(p + 27 + segments, packet, packetSize);
    if (!ready) {
        unsigned j;
        for (j = 0; j < 256; j++) {
            uint32_t v = (uint32_t) j << 24;
            int bit;
            for (bit = 0; bit < 8; bit++) {
                v = (v << 1) ^ ((v & 0x80000000) ? 0x04c11db7 : 0);
            }
            table[j] = v;
        }
        ready = true;
    }
    for (i = 0; i < total; i++) {
        crc = (crc << 8) ^ table[((crc >> 24) ^ p[i]) & 255];
    }
    fmput32(p + 22, crc);
    return true;
}

// ===[ vorbis setup packets ]===

static bool rebuildVorbisSetup(uint32_t key, uint8_t* out, size_t* bytes) {
    // the stored fragments stop before the mapping and modes. these tails
    // finish the packets; they match the complete setups in python-fsb5.
    static const uint8_t stereoTail[] = {
        0x0c, 0, 0xf0, 0, 0, 0x90, 0x5c, 0, 0x11, 0x11, 0xd1, 0xcc, 0x61, 0x64, 0x68, 0x6c,
        0x70, 0x74, 0x78, 0x7c, 0x80, 0x84, 0x88, 0x8c, 0x90, 8, 0, 0, 0, 0, 0, 0x17,
        0, 0x7c, 0, 0, 0x24, 0x25, 0x40, 0x44, 0x44, 0x34, 0x73, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
        0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 1, 0, 0x80, 0, 2, 0, 0, 0,
        0, 0x20, 0x80, 0, 4, 4, 4, 0, 0, 0, 0, 0, 2, 0, 0, 0, 4, 4
    };
    static const uint8_t monoTail[] = {
        0x60, 0, 0, 0x0f, 0, 0, 0xc7, 5, 0x10, 0x11, 0xd1, 0x1c, 0x46, 0x86, 0xc6, 6,
        0x47, 0x87, 0xc7, 7, 0x48, 0x48, 0, 0, 0, 0, 0, 0xb8, 0, 0xc0, 7, 0,
        0xc0, 0x21, 2, 0x44, 0x44, 0x34, 0x87, 0x91, 0xa1, 0xb1, 0xc1, 0xd1, 0xe1, 0xf1, 1, 0x12,
        0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 2, 0, 0, 0, 4, 4
    };
    const char* text;
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t compressed[4096];
    size_t n = 0;
    unsigned accum = 0, bits = 0;
    mz_ulong len = (mz_ulong) *bytes;
    if (key == 0xc4c30a29) {
        text = fmSetupStereo;
    } else if (key == 0x355295ca) {
        text = fmSetupMono;
    } else {
        logWarn("FMOD: Unsupported Vorbis setup CRC %08x\n", key);
        return false;
    }
    for (; *text && *text != '='; text++) {
        const char* a = strchr(alphabet, *text);
        if (!a) return false;
        accum = (accum << 6) | (unsigned) (a - alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n == sizeof(compressed)) return false;
            compressed[n++] = (uint8_t) (accum >> bits);
        }
    }
    if (mz_uncompress(out, &len, compressed, (mz_ulong) n) != MZ_OK) return false;
    {
        const uint8_t* tail = key == 0xc4c30a29 ? stereoTail : monoTail;
        size_t tailSize = key == 0xc4c30a29 ? sizeof(stereoTail) : sizeof(monoTail);
        if ((size_t) len > *bytes || tailSize > *bytes - (size_t) len) return false;
        memcpy(out + len, tail, tailSize);
        len += (mz_ulong) tailSize;
    }
    *bytes = (size_t) len;
    return true;
}

// ===[ sample reconstruction ]===

bool Fsb_rebuild(FsbSample* sample, const uint8_t* data, size_t bytes) {
    uint8_t identification[30] = {1, 'v', 'o', 'r', 'b', 'i', 's'}, comment[16] = {3, 'v', 'o', 'r', 'b', 'i', 's'};
    uint8_t setup[8192], *ogg = nullptr;
    size_t setupBytes = sizeof(setup), position = 0;
    uint32_t sequence = 0, previousBlock = 0;
    uint64_t granule = 0;
    if (sample->ogg) return true;
    if (!rebuildVorbisSetup(sample->setup, setup, &setupBytes)) return false;
    identification[11] = (uint8_t) sample->channels;
    fmput32(identification + 12, sample->rate);
    identification[28] = 0xb8;
    identification[29] = 1;
    comment[15] = 1;
    if (!appendOggPage(&ogg, identification, sizeof(identification), 0, sequence++, 2) ||
        !appendOggPage(&ogg, comment, sizeof(comment), 0, sequence++, 0) ||
        !appendOggPage(&ogg, setup, setupBytes, 0, sequence++, 0)) goto bad;
    while (position + 2 <= bytes) {
        uint32_t packetSize = fm16(data + position), blockFrames;
        bool last;
        position += 2;
        if (!packetSize) break;
        if (packetSize > bytes - position || (data[position] & 1)) goto bad;
        // these two setups use modes 0/1 for blocks of 256/2048 samples.
        blockFrames = (data[position] & 2) ? 2048 : 256;
        if (previousBlock) granule += (previousBlock + blockFrames) / 4;
        previousBlock = blockFrames;
        last = position + packetSize + 2 > bytes || fm16(data + position + packetSize) == 0;
        if (!appendOggPage(&ogg, data + position, packetSize, last ? sample->frames : granule, sequence++, last ? 4 : 0)) goto bad;
        position += packetSize;
        if (last) break;
    }
    if (sequence <= 3) goto bad;
    sample->ogg = ogg;
    sample->oggSize = (size_t) arrlen(ogg);
    return true;
bad:
    arrfree(ogg);
    return false;
}

void Fsb_free(FsbSample* samples) {
    ptrdiff_t i;
    for (i = 0; i < arrlen(samples); i++) {
        free(samples[i].name);
        arrfree(samples[i].ogg);
    }
    arrfree(samples);
}
