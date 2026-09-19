#pragma once
#include <Arduino.h>

// Simple OTA rollback helper using a boot-fail counter stored in Preferences.
// If the device fails to mark successful boot N times in a row, preferences
// are cleared (factory reset) to recover.

void otaRollback_checkBoot();
void otaRollback_markSuccess();
