#pragma once

#include <Arduino.h>
#include "Config.h"

#if SERIAL_READINGS_ONLY
class QuietSerialSink {
public:
    void begin(unsigned long) {}
    void setTimeout(unsigned long) {}
    int available() { return 0; }
    String readStringUntil(char) { return String(); }
    void flush() {}

    size_t write(uint8_t) { return 0; }
    size_t write(const uint8_t*, size_t size) { return size; }

    template <typename... Args>
    int printf(const char*, Args...) { return 0; }

    template <typename T>
    size_t print(const T&) { return 0; }

    template <typename T>
    size_t print(const T&, int) { return 0; }

    template <typename T>
    size_t println(const T&) { return 0; }

    template <typename T>
    size_t println(const T&, int) { return 0; }

    size_t println() { return 0; }
};

static QuietSerialSink SerialQuiet;
#endif
