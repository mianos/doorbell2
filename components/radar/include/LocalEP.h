#pragma once

#include <cstdint>

#include "Events.h"
#include "Settings.h"
#include "RadarPublisher.h"

// EventProc that forwards radar events to a RadarPublisher, applying the
// presence/tracking rate limits from Settings. Ported from doorbell2/lep.h.
class LocalEP : public EventProc {
public:
    LocalEP(RadarPublisher* pub, Settings* settings) : pub_(pub), settings_(settings) {}

    void Detected(Value* vv) override;
    void Cleared() override;
    void PresenceUpdate(Value* vv) override;
    void TrackingUpdate(Value* vv) override;

private:
    RadarPublisher* pub_;
    Settings*       settings_;
    uint32_t        lastTrackingUpdateTime_ = 0;
    uint32_t        lastPresenceUpdateTime_ = 0;
};
