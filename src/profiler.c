#include "profiler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"
#include "stb_ds.h"
#include "string_builder.h"
#include "gettime.h"

Profiler* Profiler_create(void) {
    Profiler* p = (Profiler *)safeMalloc(sizeof(Profiler));
    p->entries = nullptr;
    p->frameDepth = 0;
    p->instructionCount = 0;
    return p;
}

void Profiler_destroy(Profiler* p) {
    if (p == nullptr) return;
    shfree(p->entries);
    free(p);
}

void Profiler_setEnabled(Profiler** slot, bool enabled) {
    if (enabled) {
        if (*slot == nullptr) *slot = Profiler_create();
    } else {
        if (*slot != nullptr) {
            Profiler_destroy(*slot);
            *slot = nullptr;
        }
    }
}

void Profiler_enter(Profiler* p, const char* name) {
    if (p == nullptr) return;
    if (p->frameDepth >= PROFILER_MAX_DEPTH) return;
    ProfilerFrame* f = &p->frameStack[p->frameDepth];
    f->startNanos = nowNanos();
    f->childNanos = 0;
    f->startOps = p->instructionCount;
    f->childOps = 0;
    f->name = name != nullptr ? name : "<unknown>";
    p->frameDepth++;
}

void Profiler_exit(Profiler* p) {
    if (p == nullptr) return;
    if (0 >= p->frameDepth) return;
    p->frameDepth--;
    ProfilerFrame* f = &p->frameStack[p->frameDepth];
    uint64_t elapsed = nowNanos() - f->startNanos;
    uint64_t selfNanos = elapsed > f->childNanos ? elapsed - f->childNanos : 0;
    uint64_t totalOps = p->instructionCount - f->startOps;
    uint64_t selfOps = totalOps > f->childOps ? totalOps - f->childOps : 0;

    ptrdiff_t i = shgeti(p->entries, f->name);
    if (0 > i) {
        ProfilerStats stats = {0};
        stats.nanos = selfNanos;
        stats.ops = selfOps;
        shput(p->entries, f->name, stats);
    } else {
        p->entries[i].value.nanos += selfNanos;
        p->entries[i].value.ops += selfOps;
    }

    if (p->frameDepth > 0) {
        p->frameStack[p->frameDepth - 1].childNanos += elapsed;
        p->frameStack[p->frameDepth - 1].childOps += totalOps;
    }
}

static int compareEntriesDesc(const void* a, const void* b) {
    uint64_t va = ((const ProfilerEntry*) a)->value.nanos;
    uint64_t vb = ((const ProfilerEntry*) b)->value.nanos;
    if (vb > va) return 1;
    if (va > vb) return -1;
    return 0;
}

// Sort entries into a caller-owned buffer. Returns entry count; 0 if nothing to report.
// Also computes the grand total (across all entries, not just topN) in *outTotal.
static size_t collectSorted(const Profiler* p, ProfilerEntry* outSorted, size_t outCap, ProfilerStats* outTotal) {
    size_t count = shlen(p->entries);
    if (count == 0) return 0;
    if (count > outCap) count = outCap;
    memcpy(outSorted, p->entries, count * sizeof(ProfilerEntry));
    qsort(outSorted, count, sizeof(ProfilerEntry), compareEntriesDesc);

    ProfilerStats total = { 0 };
    size_t fullCount = shlen(p->entries);
    repeat(fullCount, i) {
        total.nanos += p->entries[i].value.nanos;
        total.ops += p->entries[i].value.ops;
    }
    *outTotal = total;
    return count;
}

void Profiler_reset(Profiler* p) {
    if (p == nullptr) return;
    shfree(p->entries);
    p->entries = nullptr;
}

char* Profiler_createReport(const Profiler* p, int topN, int framesInWindow) {
    if (p == nullptr) return nullptr;
    size_t count = shlen(p->entries);
    if (count == 0) return nullptr;
    if (0 >= framesInWindow) framesInWindow = 1;

    ProfilerEntry* sorted = (ProfilerEntry*) malloc(count * sizeof(ProfilerEntry));
    if (sorted == nullptr) return nullptr;
    ProfilerStats total = { 0 };
    size_t sortedEntriesCount = collectSorted(p, sorted, count, &total);

    size_t limit = sortedEntriesCount;
    if (topN > 0 && (size_t) topN < limit)
        limit = (size_t) topN;

    StringBuilder stringBuilder = StringBuilder_create(64);

    double frames = (double) framesInWindow;
    double totalMs = total.nanos / 1000000.0;
    double totalOpsPerFrame = (double) total.ops / frames;

    StringBuilder_appendFormat(&stringBuilder, "GML Profiler (avg %d frames)\n", framesInWindow);
    repeat(limit, i) {
        double perFrameMs = ((double) sorted[i].value.nanos / (double) 1000000) / frames;
        double opsPerFrame = (double) sorted[i].value.ops / frames;
        double nsPerOp = sorted[i].value.ops > 0 ? (double) sorted[i].value.nanos / (double) sorted[i].value.ops : (double) 0;
        StringBuilder_appendFormat(&stringBuilder, "%.2fms %.0f ops (%.0f ns/op) %s\n", perFrameMs, opsPerFrame, nsPerOp, sorted[i].key);
    }
    StringBuilder_appendFormat(&stringBuilder, "total %.2fms/frame, %.0f ops/frame (%zu scripts)", totalMs / frames, totalOpsPerFrame, sortedEntriesCount);
    char* result = StringBuilder_toString(&stringBuilder);
    StringBuilder_free(&stringBuilder);
    free(sorted);
    return result;
}
