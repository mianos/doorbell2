#pragma once

#include "mqtt.h"
#include "settings.h"


class LocalEP : public EventProc {
  std::shared_ptr<RadarMqtt> mqtt;
  std::shared_ptr<SettingsManager> settings;
  uint32_t lastTrackingUpdateTime = 0;
  uint32_t lastPresenceUpdateTime = 0;
public:
  LocalEP(std::shared_ptr<RadarMqtt> mqtt, std::shared_ptr<SettingsManager> settings) : mqtt(mqtt), settings(settings) {
  }

  virtual void Detected(Value *vv) {
    if (!mqtt->client.connected()) {
      return;
    }
    mqtt->mqtt_update_presence(true, vv);
  }

  virtual void Cleared() {
    if (!mqtt->client.connected()) {
      return;
    }
    mqtt->mqtt_update_presence(false);
  }

  virtual void PresenceUpdate(Value *vv) {
    if (!mqtt->client.connected()) {
      return;
    }
    uint32_t currentMillis = millis();
    if (settings->presence && (currentMillis - lastPresenceUpdateTime >= settings->presence)) {
      mqtt->mqtt_update_presence(vv->etype() != "no", vv);
      lastPresenceUpdateTime = currentMillis;
    }
  }

  virtual void TrackingUpdate(Value *vv) {
    if (!mqtt->client.connected()) {
      return;
    }
    uint32_t currentMillis = millis();
    if (settings->tracking && (currentMillis - lastTrackingUpdateTime >= settings->tracking)) {
      mqtt->mqtt_track(vv);
      lastTrackingUpdateTime = currentMillis;
    }
  }
};

