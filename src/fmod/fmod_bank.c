#include "fmod_internal.h"
#include "log.h"
#include "math_compat.h"
#include <stdlib.h>

// ===[ files and records ]===

static char* copyString(const char* string) {
    size_t bytes = strlen(string) + 1;
    char* copy = (char*) malloc(bytes);
    if (copy) memcpy(copy, string, bytes);
    return copy;
}

static char* normalizePath(const char* path) {
    char* normalized = copyString(path);
    if (normalized) {
        for (char* p = normalized; *p; p++) {
            if (*p == '\\') *p = '/';
        }
    }
    return normalized;
}

static FmBank* findBank(FmodSystem* system, const char* path) {
    for (ptrdiff_t i = 0; i < arrlen(system->banks); i++) {
        if (strcmp(system->banks[i]->path, path) == 0) return system->banks[i];
    }
    return nullptr;
}

static bool readAt(FmodSystem* system, void* file, uint32_t offset, void* destination, size_t bytes) {
    size_t done = 0;
    if (offset > INT32_MAX || bytes > INT32_MAX || !system->fs->vtable->binarySeek(system->fs, file, (int32_t) offset)) return false;
    while (done < bytes) {
        int32_t read = system->fs->vtable->binaryRead(system->fs, file, (uint8_t*) destination + done, (int32_t) (bytes - done));
        if (read <= 0 || (size_t) read > bytes - done) return false;
        done += (size_t) read;
    }
    return true;
}

static bool parseRecords(FmBank* bank, size_t start, size_t end, int parent, unsigned depth) {
    size_t position = start;
    if (depth > 32) return false;
    while (position < end) {
        Record record = {0};
        if (arrlen(bank->records) >= FM_MAX_RECORDS) return false;
        if (end - position < 8) return false;
        size_t bytes = fm32(bank->metadata + position + 4);
        if (bytes > end - position - 8) return false;
        memcpy(record.tag, bank->metadata + position, 4);
        record.data = bank->metadata + position + 8;
        record.size = bytes;
        record.parent = parent;
        record.bank = bank;
        int index = (int) arrlen(bank->records);
        if (memcmp(record.tag, "LIST", 4) == 0) {
            if (bytes < 4) return false;
            memcpy(record.tag, record.data, 4);
            record.data += 4;
            record.size -= 4;
            arrput(bank->records, record);
            if (!parseRecords(bank, position + 12, position + 8 + bytes, index, depth + 1)) return false;
        } else {
            arrput(bank->records, record);
        }
        position += 8 + bytes + (bytes & 1);
        if (position > end) return false;
    }
    return position == end;
}

static bool parseEffects(FmBank* bank) {
    for (ptrdiff_t i = 0; i < arrlen(bank->records); i++) {
        Record* owner = &bank->records[i];
        if (!tagIs(owner, "BEFB")) continue;
        if (owner->size < FM_EFFECT_SIZE) return false;
        Record* payload = findSiblingRecord(owner, "PMEF");
        if (!payload || payload->size < 4) return false;
        EffectDefinition effect = {owner, fm32(owner->data + FM_EFFECT_TYPE), owner->data[FM_EFFECT_BYPASS] != 0, nullptr};
        arrput(bank->effects, effect);
        EffectDefinition* definition = &bank->effects[arrlen(bank->effects) - 1];
        FmReader reader = {payload->data, payload->size, 4, false};
        uint32_t count = fm32(payload->data);
        if (count > FM_MAX_EFFECT_PARAMETERS) return false;
        for (uint32_t parameter = 0; parameter < count; parameter++) {
            const uint8_t* kind = fmread(&reader, 4);
            if (!kind) return false;
            EffectParameter value = {0};
            value.kind = fm32(kind);
            if (value.kind == FM_EFFECT_FLOAT || value.kind == FM_EFFECT_INTEGER) {
                const uint8_t* data = fmread(&reader, 4);
                if (!data) return false;
                if (value.kind == FM_EFFECT_FLOAT) {
                    value.value.real = fmfloat(data);
                    if (!isfinite(value.value.real)) return false;
                } else {
                    value.value.integer = (int32_t) fm32(data);
                }
            } else if (value.kind == FM_EFFECT_BOOLEAN) {
                const uint8_t* data = fmread(&reader, 1);
                if (!data || *data > 1) return false;
                value.value.boolean = *data != 0;
            } else if (value.kind == FM_EFFECT_DATA) {
                value.value.buffer.size = fmcount(&reader);
                value.value.buffer.data = fmread(&reader, value.value.buffer.size);
                if (reader.failed) return false;
            } else {
                return false;
            }
            arrput(definition->parameters, value);
        }
    }
    return true;
}

void FmodBank_destroy(FmBank* bank) {
    if (!bank) return;
    Fsb_free(bank->samples);
    for (ptrdiff_t i = 0; i < arrlen(bank->effects); i++) arrfree(bank->effects[i].parameters);
    arrfree(bank->effects);
    arrfree(bank->records);
    free(bank->metadata);
    free(bank->path);
    free(bank);
}

static FmBank* parseBank(FmodSystem* system, const char* path) {
    uint8_t riff[12], header[64];
    uint32_t position = 12, metadataEnd = 0, fsbOffset = 0, fsbBytes = 0;
    FmBank* bank = nullptr;
    uint8_t* fsb = nullptr;
    void* file = system->fs->vtable->binaryOpen(system->fs, path, GML_FILE_BIN_READ);
    if (!file) return nullptr;
    int32_t fileSize = system->fs->vtable->binarySize(system->fs, file);
    if (fileSize < 12 || !readAt(system, file, 0, riff, 12) || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "FEV ", 4) || fm32(riff + 4) != (uint32_t) fileSize - 8) goto fail;

    while (position < (uint32_t) fileSize) {
        if ((uint32_t) fileSize - position < 8 || !readAt(system, file, position, header, 8)) goto fail;
        uint32_t bytes = fm32(header + 4);
        if (bytes > (uint32_t) fileSize - position - 8) goto fail;
        if (memcmp(header, "SND ", 4) == 0) {
            if (fsbOffset) goto fail;
            metadataEnd = position;
            uint32_t scan = bytes < sizeof(header) ? bytes : (uint32_t) sizeof(header);
            if (!readAt(system, file, position + 8, header, scan)) goto fail;
            uint32_t padding;
            for (padding = 0; padding + 4 <= scan; padding++) {
                if (memcmp(header + padding, "FSB5", 4) == 0) break;
            }
            if (padding + 4 > scan) goto fail;
            fsbOffset = position + 8 + padding;
            if (bytes - padding < 60 || !readAt(system, file, fsbOffset, header, 60)) goto fail;
            if (fm32(header + 12) > FM_MAX_METADATA || fm32(header + 16) > FM_MAX_METADATA - fm32(header + 12)) goto fail;
            fsbBytes = 60 + fm32(header + 12) + fm32(header + 16);
            if (fsbBytes > bytes - padding || fm32(header + 20) > bytes - padding - fsbBytes) goto fail;
        } else if (fsbOffset) {
            goto fail;
        }
        position += 8 + bytes + (bytes & 1);
    }
    if (position != (uint32_t) fileSize) goto fail;
    if (!metadataEnd) metadataEnd = (uint32_t) fileSize;
    if (metadataEnd > FM_MAX_METADATA) goto fail;
    bank = (FmBank*) calloc(1, sizeof(*bank));
    if (!bank) goto fail;
    bank->path = copyString(path);
    bank->metadata = (uint8_t*) malloc(metadataEnd);
    if (!bank->path || !bank->metadata || !readAt(system, file, 0, bank->metadata, metadataEnd) || !parseRecords(bank, 12, metadataEnd, -1, 0)) goto fail;

    bool hasFormat = false;
    for (ptrdiff_t i = 0; i < arrlen(bank->records); i++) {
        Record* record = &bank->records[i];
        if (tagIs(record, "FMT ")) {
            if (record->size != 8 || fm32(record->data) != 142 || fm32(record->data + 4) != 140) goto fail;
            hasFormat = true;
        }
    }
    if (!hasFormat || !parseEffects(bank)) goto fail;
    if (fsbOffset) {
        uint32_t dataOffset;
        fsb = (uint8_t*) malloc(fsbBytes);
        if (!fsb || !readAt(system, file, fsbOffset, fsb, fsbBytes) || !Fsb_parse(fsb, fsbBytes, &dataOffset, &bank->samples)) goto fail;
        bank->dataOffset = fsbOffset + dataOffset;
    }
    free(fsb);
    system->fs->vtable->binaryClose(system->fs, file);
    return bank;

fail:
    free(fsb);
    FmodBank_destroy(bank);
    system->fs->vtable->binaryClose(system->fs, file);
    return nullptr;
}

// ===[ strings bank ]===

typedef struct {
    const uint8_t* nodes;
    uint32_t count, visits;
    const uint8_t* guids;
    uint32_t guidCount;
    const uint8_t* strings;
    uint32_t stringBytes;
} StringTable;

static bool indexStringNode(FmodBankIndex* index, StringTable* table, uint32_t nodeIndex, char* path, size_t length, unsigned depth) {
    if (nodeIndex >= table->count || depth > 128 || table->visits++ >= table->count) return false;
    const uint8_t* node = table->nodes + nodeIndex * 8;
    uint32_t stringOffset = fm24(node), firstChild = fm24(node + 4), childCount = node[7];
    if (stringOffset != 0xffffff) {
        if (stringOffset >= table->stringBytes) return false;
        const uint8_t* end = (const uint8_t*) memchr(table->strings + stringOffset, 0, table->stringBytes - stringOffset);
        if (!end) return false;
        size_t bytes = (size_t) (end - (table->strings + stringOffset));
        if (bytes >= 1024 - length) return false;
        memcpy(path + length, table->strings + stringOffset, bytes);
        length += bytes;
    }
    path[length] = 0;
    if (childCount) {
        if (firstChild > table->count || childCount > table->count - firstChild) return false;
        for (uint32_t i = 0; i < childCount; i++) {
            if (!indexStringNode(index, table, firstChild + i, path, length, depth + 1)) return false;
        }
    } else {
        if (firstChild >= table->guidCount) return false;
        Guid id;
        memcpy(id.b, table->guids + firstChild * FM_GUID_SIZE, FM_GUID_SIZE);
        shput(index->names, path, id);
    }
    return true;
}

static bool indexStrings(FmodBankIndex* index, Record* record) {
    FmReader reader = {record->data, record->size, 0, false};
    StringTable table = {0};
    uint32_t stride;
    char path[1024];
    const uint8_t* version = fmread(&reader, 4);
    if (!version || fm32(version) != 1) return false;
    table.nodes = fmlist(&reader, &table.count, &stride);
    if (reader.failed || stride != 8) return false;
    table.guids = fmlist(&reader, &table.guidCount, &stride);
    if (reader.failed || stride != FM_GUID_SIZE) return false;
    table.stringBytes = fmcount(&reader);
    table.strings = fmread(&reader, table.stringBytes);
    if (reader.failed) return false;
    return indexStringNode(index, &table, 0, path, 0, 0);
}

// ===[ bank indexing ]===

static bool indexBank(FmodBankIndex* index, FmBank* bank) {
    static const struct {
        const char* tag;
        size_t minimumBytes;
    } indexedRecords[] = {
        {"EVTB", FM_EVENT_PARAMETERS + 2}, {"TLNB", FM_TIMELINE_LISTS + 8},
        {"WAIB", 32}, {"MUIB", 16}, {"EVIB", 32}, {"SLNB", 20},
        {"WAV ", FM_WAVE_SAMPLE + 4}, {"INST", FM_INSTRUMENT_BUS + FM_GUID_SIZE},
        {"IBSB", FM_BUS_PARENT + FM_GUID_SIZE}, {"GBSB", FM_BUS_PARENT + FM_GUID_SIZE},
        {"MBSB", FM_BUS_PARENT + FM_GUID_SIZE}, {"PMLB", 32},
        {"PRMB", FM_PARAMETER_NAME}, {"CURV", FM_CURVE_POINTS + 2}, {"CTRL", 52},
        {"MODB", 46},
        {"MAP ", FM_GUID_SIZE + 2},
        {"BEFB", FM_EFFECT_SIZE},
    };

    for (ptrdiff_t i = 0; i < arrlen(bank->records); i++) {
        Record* record = &bank->records[i];
        if (tagIs(record, "STDT") && record->size && !indexStrings(index, record)) return false;
        for (size_t j = 0; j < sizeof(indexedRecords) / sizeof(indexedRecords[0]); j++) {
            if (!tagIs(record, indexedRecords[j].tag)) continue;
            if (record->size < indexedRecords[j].minimumBytes) return false;
            Key key;
            memcpy(key.b, record->data + (tagIs(record, "MODB") ? FM_MODULATOR_GUID : 0), FM_GUID_SIZE);
            memcpy(key.b + FM_GUID_SIZE, record->tag, 4);
            hmput(index->records, key, record);
            break;
        }
        if (tagIs(record, "PRMB")) {
            uint32_t nameBytes = fm16(record->data + FM_PARAMETER_NAME_LENGTH);
            size_t limitsOffset = FM_PARAMETER_NAME + nameBytes;
            if (nameBytes > 255 || limitsOffset + 12 > record->size) return false;
            float minimum = fmfloat(record->data + limitsOffset);
            float maximum = fmfloat(record->data + limitsOffset + 4);
            float initial = fmfloat(record->data + limitsOffset + 8);
            if (!isfinite(minimum) || !isfinite(maximum) || !isfinite(initial) || minimum > maximum || initial < minimum || initial > maximum) return false;
            if (findParameterIndex(index, record->data) >= 0) continue;
            Parameter parameter = {0};
            memcpy(parameter.id.b, record->data, FM_GUID_SIZE);
            parameter.name = (char*) malloc(nameBytes + 1);
            if (!parameter.name) return false;
            memcpy(parameter.name, record->data + FM_PARAMETER_NAME, nameBytes);
            parameter.name[nameBytes] = 0;
            parameter.global = fm32(record->data + FM_PARAMETER_GLOBAL) != 0;
            parameter.min = minimum;
            parameter.max = maximum;
            parameter.initial = parameter.value = initial;
            parameter.current = initial;
            if (record->size >= limitsOffset + 24) {
                parameter.seekUp = fmfloat(record->data + limitsOffset + 16);
                parameter.seekDown = fmfloat(record->data + limitsOffset + 20);
                if (!isfinite(parameter.seekUp) || !isfinite(parameter.seekDown) || parameter.seekUp < 0 || parameter.seekDown < 0) {
                    free(parameter.name);
                    return false;
                }
            }
            arrput(index->parameters, parameter);
        }
    }
    for (ptrdiff_t i = 0; i < arrlen(bank->records); i++) {
        Record* record = &bank->records[i];
        if (!tagIs(record, "PMLB")) continue;
        Record* controllers = findSiblingRecord(record, "CTRO");
        if (!controllers) continue;
        FmReader reader = {controllers->data, controllers->size, 0, false};
        uint32_t count, stride;
        const uint8_t* list = fmlist(&reader, &count, &stride);
        if (reader.failed || (count && stride != FM_GUID_SIZE)) return false;
        for (uint32_t j = 0; j < count; j++) {
            CurveInput input;
            memcpy(input.curve.b, list + j * FM_GUID_SIZE, FM_GUID_SIZE);
            memcpy(input.parameter.b, record->data + FM_GUID_SIZE, FM_GUID_SIZE);
            arrput(index->inputs, input);
        }
    }
    return true;
}

void FmodBank_clearIndex(FmodBankIndex* index) {
    for (ptrdiff_t i = 0; i < arrlen(index->parameters); i++) free(index->parameters[i].name);
    arrfree(index->parameters);
    arrfree(index->inputs);
    hmfree(index->records);
    shfree(index->names);
}

static bool publishBank(FmodSystem* system, FmBank* bank) {
    // don't swap anything into the live system until this bank checks out.
    FmodBankIndex staged = {0};
    sh_new_strdup(staged.names);
    for (ptrdiff_t i = 0; i < arrlen(system->banks); i++) {
        if (!indexBank(&staged, system->banks[i])) goto fail;
    }
    if (!indexBank(&staged, bank)) goto fail;

    // instances still use the old parameter indices. keep those in place,
    // along with any values the game has changed since loading the banks.
    if (arrlen(staged.parameters) < arrlen(system->index.parameters)) goto fail;
    for (ptrdiff_t i = 0; i < arrlen(system->index.parameters); i++) {
        if (!equalGuid(staged.parameters[i].id.b, system->index.parameters[i].id.b)) goto fail;
        staged.parameters[i].value = system->index.parameters[i].value;
        staged.parameters[i].current = system->index.parameters[i].current;
    }
    FmodBank_clearIndex(&system->index);
    system->index = staged;
    arrput(system->banks, bank);
    return true;

fail:
    FmodBank_clearIndex(&staged);
    return false;
}

bool Fmod_loadBank(FmodSystem* system, const char* path, bool nonblocking) {
    (void) nonblocking; // this loader finishes on the game thread, even if async was requested.
    if (!system || !path || !*path) return false;
    char* normalized = normalizePath(path);
    if (!normalized) return false;
    if (findBank(system, normalized)) {
        free(normalized);
        return true;
    }
    FmBank* bank = parseBank(system, normalized);
    free(normalized);
    if (!bank || !publishBank(system, bank)) {
        FmodBank_destroy(bank);
        logWarn("FMOD: Cannot load bank '%s' (missing, malformed, or unsupported format)\n", path);
        return false;
    }
    logInfo("FMOD: Loaded %s (%td samples)\n", path, arrlen(bank->samples));
    return true;
}

// ===[ encoded sample cache ]===

void FmodBank_evict(FmodSystem* system, FsbSample* protectedSample) {
    while (system->cacheBytes > FM_CACHE_LIMIT) {
        FsbSample* oldest = nullptr;
        for (ptrdiff_t i = 0; i < arrlen(system->banks); i++) {
            for (ptrdiff_t j = 0; j < arrlen(system->banks[i]->samples); j++) {
                FsbSample* sample = &system->banks[i]->samples[j];
                if (sample != protectedSample && sample->ogg && !sample->references && (!oldest || sample->lastUse < oldest->lastUse)) oldest = sample;
            }
        }
        if (!oldest) break;
        system->cacheBytes -= oldest->oggSize;
        arrfree(oldest->ogg);
        oldest->oggSize = 0;
    }
}

bool FmodBank_prepareSample(FmodSystem* system, FmBank* bank, int index) {
    if (index < 0 || index >= arrlen(bank->samples)) return false;
    FsbSample* sample = &bank->samples[index];
    sample->lastUse = ++system->useCounter;
    if (sample->ogg) return true;
    uint8_t* packets = (uint8_t*) malloc(sample->bytes);
    if (!packets) return false;
    void* file = system->fs->vtable->binaryOpen(system->fs, bank->path, GML_FILE_BIN_READ);
    bool loaded = file && readAt(system, file, bank->dataOffset + sample->offset, packets, sample->bytes) && Fsb_rebuild(sample, packets, sample->bytes);
    if (file) system->fs->vtable->binaryClose(system->fs, file);
    free(packets);
    if (loaded) {
        system->cacheBytes += sample->oggSize;
        FmodBank_evict(system, sample);
    } else {
        logWarn("FMOD: Could not decode %s sample %d\n", bank->path, index);
    }
    return loaded;
}

bool Fmod_loadSamples(FmodSystem* system, const char* path) {
    if (!Fmod_loadBank(system, path, false)) return false;
    char* normalized = normalizePath(path);
    if (!normalized) return false;
    FmBank* bank = findBank(system, normalized);
    free(normalized);
    if (!bank) return false;
    for (ptrdiff_t i = 0; i < arrlen(bank->samples); i++) {
        if (!FmodBank_prepareSample(system, bank, (int) i)) return false;
    }
    return true;
}
