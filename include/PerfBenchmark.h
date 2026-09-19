#ifndef PERF_BENCHMARK_H
#define PERF_BENCHMARK_H

#include <Arduino.h>

struct TimingStats {
    const char* name;
    unsigned long minUs;
    unsigned long maxUs;
    unsigned long totalUs;
    uint32_t count;
    unsigned long lastUs;
};

void perfBegin();
void perfPrintStats();
void perfResetStats();

// Timing scope helper - measures block execution time
class PerfScope {
public:
    explicit PerfScope(const char* name);
    ~PerfScope();

private:
    const char* m_name;
    unsigned long m_startUs;
};

// Convenience macros for timing measurement
#define PERF_SCOPE(name) PerfScope perfScope_##__LINE__(name)
#define PERF_BEGIN(name) perfStartTimer(name)
#define PERF_END(name) perfEndTimer(name)

void perfStartTimer(const char* name);
void perfEndTimer(const char* name);

// Get current heap stats
struct HeapStats {
    uint32_t freeHeap;
    uint32_t minFreeHeap;
    uint32_t largestBlock;
};

HeapStats perfGetHeapStats();

// Print comprehensive performance report
void perfPrintFullReport();

#endif // PERF_BENCHMARK_H
