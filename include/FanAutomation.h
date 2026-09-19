#ifndef FAN_AUTOMATION_H
#define FAN_AUTOMATION_H

#include "Config.h"

namespace automation {

// Thermostat with hysteresis to prevent relay chatter.
//
// When the fan is OFF (currentState == false):
//   Fan turns ON  when airTemperature >= airTemperatureMaximum
//
// When the fan is ON (currentState == true):
//   Fan turns OFF when airTemperature <= (airTemperatureMaximum - FAN_HYSTERESIS_C)
//   Fan stays  ON  when airTemperature >  (airTemperatureMaximum - FAN_HYSTERESIS_C)
//
// The deadband between ON and OFF thresholds is FAN_HYSTERESIS_C degrees,
// matching the WATER_LEVEL_RECOVERY_MARGIN_PCT pattern used for the water
// level safety lock.
inline bool coolingFanShouldRun(float airTemperature, float airTemperatureMaximum,
                               bool currentState = false) {
    if (currentState) {
        // Fan is running: keep running until temperature drops to or below the
        // off threshold to prevent immediate on/off cycling.
        return airTemperature > (airTemperatureMaximum - FAN_HYSTERESIS_C);
    } else {
        // Fan is off: turn on only when temperature reaches the threshold.
        return airTemperature >= airTemperatureMaximum;
    }
}

} // namespace automation

#endif // FAN_AUTOMATION_H
