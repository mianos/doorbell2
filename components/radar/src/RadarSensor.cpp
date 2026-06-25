#include "RadarSensor.h"
#include "RadarTime.h"

RadarSensor::RadarSensor(EventProc* ep, Settings* settings)
    : ep(ep), settings(settings) {}

void RadarSensor::process(float minPower) {
    auto valuesList = get_decoded_radar_data();

    bool noTargetFound = true;
    for (auto& v : valuesList) {
        if (v->get_power() >= minPower) {
            noTargetFound = false;
            break;
        }
    }

    // If tracking is on, don't send a second update when a detection event fired.
    bool sent_detected_event = false;
    switch (currentState) {
        case STATE_NOT_DETECTED:
            if (!noTargetFound) {
                for (auto& v : valuesList) {
                    if (v->isTarget()) {
                        ep->Detected(v.get());
                        sent_detected_event = true;
                    }
                }
                currentState = STATE_DETECTED_ONCE;
            }
            break;

        case STATE_DETECTED_ONCE:
            currentState = STATE_DETECTED;
            lastDetectionTime = radar_millis();
            break;

        case STATE_DETECTED:
            if (noTargetFound) {
                if (radar_millis() - lastDetectionTime > (uint32_t)settings->detectionTimeout) {
                    ep->Cleared();
                    currentState = STATE_CLEARED_ONCE;
                }
            } else {
                lastDetectionTime = radar_millis();
            }
            break;

        case STATE_CLEARED_ONCE:
            currentState = STATE_NOT_DETECTED;
            break;
    }

    if (!sent_detected_event) {
        for (auto& v : valuesList) {
            if (v->isTarget()) {
                ep->TrackingUpdate(v.get());
                ep->PresenceUpdate(v.get());
            }
        }
    }
}
