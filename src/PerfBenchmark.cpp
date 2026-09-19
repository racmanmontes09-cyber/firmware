#include "PerfBenchmark.h"
#include "QuietSerial.h"
#include <esp_task_wdt.h>

#if SERIAL_READINGS_ONLY
#define Serial SerialQuiet
#endif

static const int MAX_TIMERS = 20;
static TimingStats g_stats[MAX_TIMERS];
static int g_timerCount = 0;

// Simple stack for nested timing
static const int MAX_NESTING = 10;
static struct {
    const char* name;
    unsigned long startUs;
} g_nestingStack[MAX_NESTING];
static int g_nestingDepth = 0;

void perfBegin() {
    g_timerCount = 0;
    g_nestingDepth = 0;
    memset(g_stats, 0, sizeof(g_stats));
    Serial.println("[PERF] Benchmark system initialized");
}

static TimingStats* findOrCreateTimer(const char* name) {
    for (int i = 0; i < g_timerCount; ++i) {
        if (g_stats[i].name == name) {
            return &g_stats[i];
        }
    }
    if (g_timerCount < MAX_TIMERS) {
        g_stats[g_timerCount].name = name;
        g_stats[g_timerCount].minUs = UINT32_MAX;
        g_stats[g_timerCount].maxUs = 0;
        g_stats[g_timerCount].totalUs = 0;
        g_stats[g_timerCount].count = 0;
        g_stats[g_timerCount].lastUs = 0;
        return &g_stats[g_timerCount++];
    }
    return nullptr;
}

void perfStartTimer(const char* name) {
    if (g_nestingDepth < MAX_NESTING) {
        g_nestingStack[g_nestingDepth].name = name;
        g_nestingStack[g_nestingDepth].startUs = micros();
        g_nestingDepth++;
    }
}

void perfEndTimer(const char* name) {
    if (g_nestingDepth > 0) {
        g_nestingDepth--;
        unsigned long elapsed = micros() - g_nestingStack[g_nestingDepth].startUs;
        const char* timerName = g_nestingStack[g_nestingDepth].name;
        
        TimingStats* stat = findOrCreateTimer(timerName);
        if (stat) {
            if (elapsed < stat->minUs) stat->minUs = elapsed;
            if (elapsed > stat->maxUs) stat->maxUs = elapsed;
            stat->totalUs += elapsed;
            stat->count++;
            stat->lastUs = elapsed;
        }
    }
}

PerfScope::PerfScope(const char* name) : m_name(name), m_startUs(micros()) {}

PerfScope::~PerfScope() {
    unsigned long elapsed = micros() - m_startUs;
    TimingStats* stat = findOrCreateTimer(m_name);
    if (stat) {
        if (elapsed < stat->minUs) stat->minUs = elapsed;
        if (elapsed > stat->maxUs) stat->maxUs = elapsed;
        stat->totalUs += elapsed;
        stat->count++;
        stat->lastUs = elapsed;
    }
}

void perfPrintStats() {
    Serial.println("\n=== Performance Timing Statistics ===");
    Serial.printf("%-25s %8s %8s %8s %8s %8s\n", "Timer", "Count", "Min(us)", "Max(us)", "Avg(us)", "Last(us)");
    Serial.println("--------------------------------------------------------------------------");
    
    for (int i = 0; i < g_timerCount; ++i) {
        unsigned long avg = g_stats[i].count > 0 ? g_stats[i].totalUs / g_stats[i].count : 0;
        Serial.printf("%-25s %8u %8lu %8lu %8lu %8lu\n",
            g_stats[i].name,
            g_stats[i].count,
            g_stats[i].minUs == UINT32_MAX ? 0UL : g_stats[i].minUs,
            g_stats[i].maxUs,
            avg,
            g_stats[i].lastUs);
    }
    Serial.println();
}

void perfResetStats() {
    for (int i = 0; i < g_timerCount; ++i) {
        g_stats[i].minUs = UINT32_MAX;
        g_stats[i].maxUs = 0;
        g_stats[i].totalUs = 0;
        g_stats[i].count = 0;
        g_stats[i].lastUs = 0;
    }
    Serial.println("[PERF] Statistics reset");
}

HeapStats perfGetHeapStats() {
    HeapStats stats;
    stats.freeHeap = ESP.getFreeHeap();
    stats.minFreeHeap = ESP.getMinFreeHeap();
    stats.largestBlock = ESP.getMaxAllocHeap();
    return stats;
}

void perfPrintFullReport() {
    HeapStats heap = perfGetHeapStats();
    
    Serial.println("\n========================================");
    Serial.println("     LEAF FIRMWARE PERFORMANCE REPORT");
    Serial.println("========================================");
    
    Serial.println("\n--- Memory Usage ---");
    Serial.printf("Free Heap:          %u bytes\n", heap.freeHeap);
    Serial.printf("Min Free Heap:      %u bytes\n", heap.minFreeHeap);
    Serial.printf("Largest Free Block: %u bytes\n", heap.largestBlock);
    Serial.printf("Heap Fragmentation: %.1f%%\n", 
        heap.freeHeap > 0 ? (1.0f - (float)heap.largestBlock / heap.freeHeap) * 100.0f : 0.0f);
    
    Serial.println("\n--- Timing Statistics ---");
    perfPrintStats();
    
    Serial.println("\n--- Loop Timing ---");
    // Find and display loop-specific stats if available
    for (int i = 0; i < g_timerCount; ++i) {
        if (strcmp(g_stats[i].name, "loop_total") == 0 ||
            strcmp(g_stats[i].name, "sensors_read") == 0 ||
            strcmp(g_stats[i].name, "mqtt_publish") == 0) {
            unsigned long avg = g_stats[i].count > 0 ? g_stats[i].totalUs / g_stats[i].count : 0;
            Serial.printf("%s: avg=%luus min=%luus max=%luus\n",
                g_stats[i].name, avg,
                g_stats[i].minUs == UINT32_MAX ? 0UL : g_stats[i].minUs,
                g_stats[i].maxUs);
        }
    }
    
    Serial.println("\n========================================");
    Serial.println("           END PERFORMANCE REPORT");
    Serial.println("========================================\n");
}
