#include "TelemetryQueue.h"
#include <SPIFFS.h>
#include <Preferences.h>
#include "Logging.h"

static const char* kQueueDir = "/telemetry";
static bool s_spiffsReady = false;

bool TelemetryQueue::begin() {
    if (s_spiffsReady) return true;

    // Mount WITHOUT auto-format first. Formatting on every failed mount hides
    // real corruption and adds multi-second flash churn inside early boot.
    if (!SPIFFS.begin(false)) {
        LOG_WARN("[Queue] SPIFFS unformatted/corrupt; formatting once");
        if (!SPIFFS.begin(true)) {
            LOG_ERROR("[Queue] SPIFFS mount failed even after format");
            return false;
        }
    }
    if (!SPIFFS.exists(kQueueDir)) {
        SPIFFS.mkdir(kQueueDir);
    }
    s_spiffsReady = true;
    return true;
}

static String nextFilename() {
    Preferences p;
    p.begin("leaf_q", false);
    int idx = p.getInt("idx", 0);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s/%08d.json", kQueueDir, idx);
    p.putInt("idx", idx + 1);
    p.end();
    return String(buf);
}

bool TelemetryQueue::enqueue(const char* payload) {
    String fn = nextFilename();
    File f = SPIFFS.open(fn, FILE_WRITE);
    if (!f) {
        LOG_WARN("[Queue] Failed to open %s", fn.c_str());
        return false;
    }
    f.print(payload);
    f.close();
    LOG_DEBUG("[Queue] Enqueued %s", fn.c_str());
    return true;
}

int TelemetryQueue::count() {
    int c = 0;
    File root = SPIFFS.open(kQueueDir);
    if (!root) return 0;
    File file = root.openNextFile();
    while (file) {
        c++;
        file = root.openNextFile();
    }
    return c;
}

int TelemetryQueue::clear() {
    if (!begin()) {
        LOG_WARN("[Queue] SPIFFS mount failed during clear");
        return 0;
    }

    File root = SPIFFS.open(kQueueDir);
    if (!root) return 0;

    int removed = 0;
    File file = root.openNextFile();
    while (file) {
        String name = file.path();
        if (name.isEmpty()) name = file.name();
        if (!name.startsWith("/")) name = String(kQueueDir) + "/" + name;
        file.close();
        if (SPIFFS.remove(name)) {
            ++removed;
        } else {
            LOG_WARN("[Queue] Failed to remove %s", name.c_str());
        }
        file = root.openNextFile();
        yield();
    }

    return removed;
}

void TelemetryQueue::debugList() {
    File root = SPIFFS.open(kQueueDir);
    if (!root) return;
    File file = root.openNextFile();
    while (file) {
        LOG_DEBUG("[Queue] %s", file.name());
        file = root.openNextFile();
    }
}
