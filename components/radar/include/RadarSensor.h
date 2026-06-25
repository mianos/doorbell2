#pragma once

#include <vector>
#include <memory>

#include "Events.h"
#include "Settings.h"

// Detection state machine, ported verbatim from doorbell2/radar.cpp.
// Subclasses supply decoded radar values; this drives Detected/Cleared/
// Tracking/Presence callbacks via the EventProc.
class RadarSensor {
public:
    RadarSensor(EventProc* ep, Settings* settings);
    virtual ~RadarSensor() = default;

    virtual std::vector<std::unique_ptr<Value>> get_decoded_radar_data() = 0;
    void process(float minPower = 0.0f);

protected:
    enum DetectionState {
        STATE_NOT_DETECTED,
        STATE_DETECTED_ONCE,
        STATE_DETECTED,
        STATE_CLEARED_ONCE
    };

    EventProc*     ep;
    Settings*      settings;
    DetectionState currentState     = STATE_NOT_DETECTED;
    uint32_t       lastDetectionTime = 0;
};
