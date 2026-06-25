#pragma once

#include "Events.h"

// Sink for radar events. Implemented by the app (e.g. over MQTT), keeping the
// radar component independent of the transport.
class RadarPublisher {
public:
    virtual ~RadarPublisher() = default;
    virtual void publishPresence(bool entry, const Value* v) = 0;
    virtual void publishTracking(const Value* v)             = 0;
};
