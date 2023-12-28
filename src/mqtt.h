#pragma once
#include <memory>
#include <WiFi.h>
#include <PubSubClient.h>

#include "AudioTools.h"

struct RadarMqtt {
  WiFiClient espClient;
  PubSubClient client;
  std::unique_ptr<StreamCopy> copier;
  const char *server;
  int port = 1883;
  const String name = "doorbell2";

  unsigned long lastTimeCalled = 0;  // Store the last time the function was called
  const unsigned long interval = 250;  // Interval in milliseconds (1000 ms / 4 = 250 ms)

  void callback(char* topic_str, byte* payload, unsigned int length);
  RadarMqtt(std::unique_ptr<StreamCopy> copier, const char *server="mqtt2.mianos.com", int port=1883);

  bool reconnect();
  void handle();
};

