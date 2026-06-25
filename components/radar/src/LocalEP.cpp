#include "LocalEP.h"
#include "RadarTime.h"

void LocalEP::Detected(Value* vv) {
    pub_->publishPresence(true, vv);
}

void LocalEP::Cleared() {
    pub_->publishPresence(false, nullptr);
}

void LocalEP::PresenceUpdate(Value* vv) {
    uint32_t now = radar_millis();
    if (settings_->presence && (now - lastPresenceUpdateTime_ >= (uint32_t)settings_->presence)) {
        pub_->publishPresence(vv->isTarget(), vv);
        lastPresenceUpdateTime_ = now;
    }
}

void LocalEP::TrackingUpdate(Value* vv) {
    uint32_t now = radar_millis();
    if (settings_->tracking && (now - lastTrackingUpdateTime_ >= (uint32_t)settings_->tracking)) {
        pub_->publishTracking(vv);
        lastTrackingUpdateTime_ = now;
    }
}
