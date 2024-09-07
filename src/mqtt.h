#pragma once
#include <memory>
#include <WiFi.h>
#include <PubSubClient.h>

#include "AudioTools.h"

#include "settings.h"
#include "events.h"

struct RadarMqtt {
  WiFiClient espClient;
  PubSubClient client;
  std::unique_ptr<StreamCopy> copier;
  std::shared_ptr<SettingsManager> settings;
  EncodedAudioStream& to;
  URLStream& from;
  VolumeStream& volume;
  float prev_volume = 1.0;

  unsigned long lastTimeCalled = 0;  // Store the last time the function was called
  const unsigned long interval = 250;  // Interval in milliseconds (1000 ms / 4 = 250 ms)

  void callback(char* topic_str, byte* payload, unsigned int length);
  RadarMqtt(std::unique_ptr<StreamCopy> copier, std::shared_ptr<SettingsManager> settings, EncodedAudioStream& to, URLStream& from, VolumeStream& volume);

  bool reconnect();
  void handle();

  void mqtt_update_presence(bool entry, const Value *vv=nullptr);
  void mqtt_track(const Value *vv);
  void update_status();
};

