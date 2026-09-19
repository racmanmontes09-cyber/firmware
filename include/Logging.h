#pragma once
#include <Arduino.h>
#include "Config.h"

#if SERIAL_READINGS_ONLY && !SERIAL_NETWORK_DEBUG
#define LOG_DEBUG(...) (void)0
#define LOG_INFO(...) (void)0
#define LOG_WARN(...) (void)0
#define LOG_ERROR(...) (void)0
#else
#ifdef CFG_PRODUCTION
#define LOG_DEBUG(...) (void)0
#define LOG_INFO(...) (void)0
#else
#define LOG_DEBUG(fmt, ...) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#endif

#define LOG_WARN(fmt, ...) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif
