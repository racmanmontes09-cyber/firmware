#include "OtaRollback.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

static const char* PREF_NS = "leaf";
static const char* KEY_BOOT_FAILS = "boot_fail_count";
static const char* KEY_PENDING = "pending_ota";
static const int MAX_BOOT_FAILS = 3;

void otaRollback_checkBoot() {
    Preferences p;
    p.begin(PREF_NS, false);
    bool pending = p.getBool(KEY_PENDING, false);
    int fails = p.getInt(KEY_BOOT_FAILS, 0);
    Serial.printf("[OTA] rollback check: pending=%d fails=%d max=%d\n",
                  pending ? 1 : 0, fails, MAX_BOOT_FAILS);
    if (pending) {
        fails++;
        p.putInt(KEY_BOOT_FAILS, fails);
        if (fails >= MAX_BOOT_FAILS) {
            // Take recovery action: clear preferences (factory reset)
            Serial.println("[OTA] boot failed 3x after OTA - factory reset");
            p.end();
            Preferences prefs;
            prefs.begin(PREF_NS, false);
            prefs.clear();
            prefs.end();
            Preferences cal;
            cal.begin("leaf_cal", false);
            cal.clear();
            cal.end();
            // Reset pending flag just in case and restart
            esp_restart();
        }
    } else {
        // Not pending: reset fail counter
        p.putInt(KEY_BOOT_FAILS, 0);
    }
    p.end();
}

void otaRollback_markSuccess() {
    Preferences p;
    p.begin(PREF_NS, false);
    p.putBool(KEY_PENDING, false);
    p.putInt(KEY_BOOT_FAILS, 0);
    p.end();
}
