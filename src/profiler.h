#ifndef _BS_PROFILER_H_
#define _BS_PROFILER_H_

#include <common.h>
#include <stdint.h>
#include <stdio.h>

// GML script profiler.
// Tracks self-time (exclusive of nested script calls) and self-instruction-count per code name.

#define PROFILER_MAX_DEPTH 256

typedef struct {
    uint64_t nanos; // accumulated self-time in nanoseconds
    uint64_t ops;   // accumulated self-count of VM instructions executed
} ProfilerStats;

typedef struct {
    const char* key;
    ProfilerStats value;
} ProfilerEntry;

typedef struct {
    uint64_t startNanos;
    uint64_t childNanos;
    uint64_t startOps;
    uint64_t childOps;
    const char* name;
} ProfilerFrame;

typedef struct Profiler {
    ProfilerEntry* entries; // stb_ds sh-map
    ProfilerFrame frameStack[PROFILER_MAX_DEPTH];
    int frameDepth;
    uint64_t instructionCount;
} Profiler;

// Allocates and initializes a Profiler. Caller owns the returned pointer.
Profiler* Profiler_create(void);

// Frees the Profiler.
void Profiler_destroy(Profiler* p);

// Creates a Profiler at *slot when enabling, destroys it when disabling. Safe to call repeatedly.
void Profiler_setEnabled(Profiler** slot, bool enabled);

void Profiler_enter(Profiler* p, const char* name);
void Profiler_exit(Profiler* p);
// Allocates and returns a compact profiler summary (header + top N lines + footer). Caller owns the returned buffer and must free() it.
// Returns nullptr if there's nothing to report.
char* Profiler_createReport(const Profiler* p, int topN, int framesInWindow);
// Clears all accumulated per-script timings.
void Profiler_reset(Profiler* p);

// Record a single VM instruction.
static inline void Profiler_tickInstruction(Profiler* p) {
    if (p != nullptr) p->instructionCount++;
}

#endif /* _BS_PROFILER_H_ */
