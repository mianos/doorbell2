#include <ESPDateTime.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <StringSplitter.h>
#include <memory>

#include "AudioTools.h"

#include "provision.h"

#include "mqtt.h"

enum State {
    NOT_COPYING,
    COPYING,
    WAITING_FOR_FLUSH
} currentState = NOT_COPYING;


unsigned long lastCopyTime = 0;
const unsigned long flushDelay = 1000; // 1000 ms flush delay


void RadarMqtt::callback(char* topic_str, byte* payload, unsigned int length) {
  auto topic = String(topic_str);
  auto splitter = StringSplitter(topic, '/', 4);
  int itemCount = splitter.getItemCount();
  if (itemCount < 3) {
    Serial.printf("Item count less than 3 %d '%s'\n", itemCount, topic_str);
    return;
  }
#if 0
  for (int i = 0; i < itemCount; i++) {
    String item = splitter.getItemAtIndex(i);
    Serial.println("Item @ index " + String(i) + ": " + String(item));
  }
  Serial.printf("command '%s'\n", splitter.getItemAtIndex(itemCount - 1).c_str());
#endif
  
  if (splitter.getItemAtIndex(0) == "cmnd") {
    DynamicJsonDocument jpl(1024);
    auto err = deserializeJson(jpl, payload, length);
    if (err) {
      Serial.printf("deserializeJson() failed: '%s'\n", err.c_str());
      return;
    }
    String output;
    serializeJson(jpl, output);
    auto dest = splitter.getItemAtIndex(itemCount - 1);
    if (dest == "reprovision") {
        Serial.printf("clearing provisioning\n");
        reset_provisioning();
    } else if (dest == "restart") {
        Serial.printf("rebooting\n");
        ESP.restart();
    } else if (dest == "settings") {
      auto result = settings->loadFromDocument(jpl);
      if (std::find(result.begin(), result.end(), SettingsManager::SettingChange::VolumeChanged) != result.end()) {
        volume.setVolume(settings->volume / 100);
      }
    } else if (dest == "play") {
      if (currentState == NOT_COPYING) {
        if (jpl.containsKey("url")) {
          
          prev_volume = volume.volume();
          if (jpl.containsKey("volume")) {
            volume.setVolume(jpl["volume"].as<float>() / 100);
          }
          auto url = jpl["url"].as<String>();
          to.begin();
          from.begin(url.c_str());
        }
      } else {
        Serial.printf("already in progress, ignoring\n");
      }
    }
  }
}


RadarMqtt::RadarMqtt(std::unique_ptr<StreamCopy> copier, std::shared_ptr<SettingsManager> settings, EncodedAudioStream& to, URLStream& from, VolumeStream& volume)
    : settings(settings), client(espClient),  copier(std::move(copier)), to(to), from(from), volume(volume) {
  client.setServer(settings->mqttServer.c_str(), settings->mqttPort);
  Serial.printf("init mqtt, server '%s'\n", settings->mqttServer.c_str());
  client.setCallback([this](char* topic_str, byte* payload, unsigned int length) {
    callback(topic_str, payload, length);
  });
}


bool RadarMqtt::reconnect() {
  String clientId =  WiFi.getHostname(); // name + '-' + String(random(0xffff), HEX);
  Serial.printf("Attempting MQTT connection... to %s name %s\n", settings->mqttServer.c_str(), clientId.c_str());
  if (client.connect(clientId.c_str())) {
    delay(1000);
    String cmnd_topic = String("cmnd/") + settings->sensorName + "/#";
    client.subscribe(cmnd_topic.c_str());
    Serial.printf("mqtt connected\n");

    StaticJsonDocument<200> doc;
    doc["version"] = 2;
    doc["time"] = DateTime.toISOString();
    doc["hostname"] = WiFi.getHostname();
    doc["ip"] = WiFi.localIP().toString();
    String status_topic = "tele/" + settings->sensorName + "/init";
    String output;
    serializeJson(doc, output);
    client.publish(status_topic.c_str(), output.c_str());
    return true;
  } else {
    Serial.printf("failed to connect to %s port %d state %d\n", settings->mqttServer.c_str(), settings->mqttPort, client.state());
    delay(10000);
    return false;
  }
}

void RadarMqtt::handle() {
  if (!client.connected()) {
    if (!reconnect()) {
      return;
    }
  }
  client.loop();  // mqtt client loop
  switch (currentState) {
  case NOT_COPYING:
      if (copier->available()) {
          currentState = COPYING;
      }
      break;
  case COPYING:
      if (copier->available()) {
          copier->copy();
          lastCopyTime = millis();
      } else {
          currentState = WAITING_FOR_FLUSH;
      }
      break;
  case WAITING_FOR_FLUSH:
      if (millis() - lastCopyTime >= flushDelay) {
          to.end(); // flush output
          volume.setVolume(prev_volume);
          currentState = NOT_COPYING;
      }
      break;
  }
}



void RadarMqtt::mqtt_update_presence(bool entry, const Value *vv) {
  StaticJsonDocument<300> doc;
  doc["entry"] = entry;
  doc["time"] = DateTime.toISOString();
  if (entry) {
    vv->toJson(doc);
  }
  String status_topic = "tele/" + settings->sensorName + "/presence";
  String output;
  serializeJson(doc, output);
//  Serial.printf("sending '%s' to '%s'\n", output.c_str(), status_topic.c_str());
  client.publish(status_topic.c_str(), output.c_str());
}

void RadarMqtt::mqtt_track(const Value *vv) {
  StaticJsonDocument<300> doc;
  doc["time"] = DateTime.toISOString();
  vv->toJson(doc);
  String status_topic = "tele/" + settings->sensorName + "/tracking";
  String output;
  serializeJson(doc, output);
//  Serial.printf("sending '%s' to '%s'\n", output.c_str(), status_topic.c_str());
  client.publish(status_topic.c_str(), output.c_str());
}

