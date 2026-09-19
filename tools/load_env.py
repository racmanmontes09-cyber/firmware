"""
Inject LEAF_* configuration from the project .env file into the firmware build.

Why this exists (and why it can't use ${sysenv.LEAF_*} alone):
  platformio.ini layers compile-time config via ${sysenv.LEAF_*}, but PlatformIO
  expands that reference from os.environ at environment *initialization* time,
  BEFORE any extra_scripts pre: hook runs. When the build is launched with plain
  `pio run` (no build.sh wrapper that does `set -a; source .env`), the variables
  are not yet in os.environ, so every macro compiled to an empty string:
  MQTT_HOST="", DEVICE_ID="", empty topics, etc.

  This pre script re-reads .env and injects the real values as CPPDEFINES with
  correctly-escaped quotes. Credentials live only in .env; nothing is hardcoded
  here. The LEAF_* -D entries were removed from platformio.ini build_flags so
  there is no later empty override fighting these.
"""
import os

Import("env")  # noqa: F821  (PlatformIO injects the env)

_DEFINE_MAP = {
    "LEAF_WIFI_SSID": "WIFI_SSID",
    "LEAF_WIFI_PASSWORD": "WIFI_PASSWORD",
    "LEAF_DEVICE_ID": "DEVICE_ID",
    "LEAF_DEVICE_NAME": "DEVICE_NAME",
    "LEAF_MQTT_HOST": "MQTT_HOST",
    "LEAF_MQTT_USERNAME": "MQTT_USERNAME",
    "LEAF_MQTT_PASSWORD": "MQTT_PASSWORD",
    "LEAF_MQTT_CLIENT_ID": "MQTT_CLIENT_ID",
    "LEAF_MQTT_TELEMETRY_TOPIC": "MQTT_TELEMETRY_TOPIC",
    "LEAF_MQTT_STATUS_TOPIC": "MQTT_STATUS_TOPIC",
    "LEAF_MQTT_COMMAND_TOPIC": "MQTT_COMMAND_TOPIC",
    "LEAF_MQTT_RESULT_TOPIC": "MQTT_RESULT_TOPIC",
    "LEAF_MQTT_SETTINGS_TOPIC": "MQTT_SETTINGS_TOPIC",
    "LEAF_PH_SENSOR_ENABLED": "PH_SENSOR_ENABLED",
    "LEAF_EC_SENSOR_ENABLED": "EC_SENSOR_ENABLED",
    "LEAF_WATER_LEVEL_SENSOR_ENABLED": "WATER_LEVEL_SENSOR_ENABLED",
    "LEAF_FLOW_SENSOR_ENABLED": "FLOW_SENSOR_ENABLED",
}


def load_dotenv(path):
    values = {}
    if not os.path.isfile(path):
        return values
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip()
            # Strip one layer of surrounding quotes if present ("..." or '...').
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            if key:
                values[key] = value
    return values


env_file = os.path.join(env.subst("$PROJECT_DIR"), ".env")
dotenv = load_dotenv(env_file)

# Populate the process env so any late ${sysenv...}/{...} lookups also resolve.
for k, v in dotenv.items():
    os.environ[k] = v

# Emit string defines with escaped inner quotes so values containing spaces
# (e.g. DEVICE_NAME="Greenhouse ESP32") compile as a single quoted token.
defines = []
for leaf_key, define_name in _DEFINE_MAP.items():
    value = dotenv.get(leaf_key)
    if value is not None and value != "":
        defines.append((define_name, '\\"' + value + '\\"'))

if dotenv.get("LEAF_MQTT_PORT"):
    try:
        defines.append(("MQTT_PORT", str(int(dotenv["LEAF_MQTT_PORT"]))))
    except ValueError:
        pass

if defines:
    env.Append(CPPDEFINES=defines)
