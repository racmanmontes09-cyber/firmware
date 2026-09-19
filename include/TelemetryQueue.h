#pragma once
#include <Arduino.h>

class TelemetryQueue {
public:
    static bool begin();
    static bool enqueue(const char* payload);
    static int count();
    static int clear();
    static void debugList();
};
