#include <ESPDateTime.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <StringSplitter.h>
#include <memory>

#include "AudioTools.h"

#include "mqtt.h"


void RadarMqtt::callback(char* topic_str, byte* payload, unsigned int length) {
  Serial.printf("in '%s'\n", topic_str);
  auto topic = String(topic_str);
  auto splitter = StringSplitter(topic, '/', 4);
  int itemCount = splitter.getItemCount();
  if (itemCount < 3) {
    Serial.printf("Item count less than 3 %d '%s'\n", itemCount, topic_str);
    return;
  }
#if 1
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
#if 0
    if (dest == "tracking") {
      if (jpl.containsKey("interval")) {
        settings->tracking = jpl["interval"].as<int>();
        Serial.printf("Setting tracking interval to %d\n", settings->tracking);
      }
    }
#endif
  }
}


RadarMqtt::RadarMqtt(std::unique_ptr<AudioPlayer> player, const String& server, int port) : server(server), port(port), client(espClient),  player(std::move(player)) {
  client.setServer(server.c_str(), port);
  Serial.printf("init mqtt, server '%s'\n", server.c_str());
  client.setCallback([this](char* topic_str, byte* payload, unsigned int length) {
    callback(topic_str, payload, length);
  });
}


bool RadarMqtt::reconnect() {
  String clientId = name + '-' + String(random(0xffff), HEX);
  Serial.printf("Attempting MQTT connection... to %s name %s\n", server.c_str(), clientId.c_str());
  if (client.connect(clientId.c_str())) {
    String cmnd_topic = String("cmnd/") + name + "/#";
    client.subscribe(cmnd_topic.c_str());
    Serial.printf("mqtt connected\n");

    StaticJsonDocument<200> doc;
    doc["version"] = 1;
    doc["time"] = DateTime.toISOString();
    doc["hostname"] = WiFi.getHostname();
    doc["ip"] = WiFi.localIP().toString();
    String status_topic = "tele/" + name + "/init";
    String output;
    serializeJson(doc, output);
    client.publish(status_topic.c_str(), output.c_str());
    return true;
  } else {
    Serial.printf("failed to connect to %s port %d state %d\n", server.c_str(), port, client.state());
    return false;
  }
}

void RadarMqtt::handle() {
  if (!client.connected()) {
    if (!reconnect()) {
      return;
    }
  }
  client.loop();
  player->copy();
}
