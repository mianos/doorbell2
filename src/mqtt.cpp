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
    if (dest == "reprovision") {
        Serial.printf("clearing provisioning\n");
        wifi_prov_mgr_reset_provisioning();
        ESP.restart();
    } else if (dest == "play") {
      if (jpl.containsKey("url")) {
        auto url = jpl["url"].as<String>();
        Serial.printf("playing '%s'\n", url.c_str());
        auto url_streamer = copier->getFrom();
        auto urlStream = static_cast<URLStream*>(url_streamer);

        urlStream->begin(url.c_str());
      }
    }
  }
}


RadarMqtt::RadarMqtt(std::unique_ptr<StreamCopy> copier, const char *server, int port) : server(server), port(port), client(espClient),  copier(std::move(copier)) {
  client.setServer(server, port);
  Serial.printf("init mqtt, server '%s'\n", server);
  client.setCallback([this](char* topic_str, byte* payload, unsigned int length) {
    callback(topic_str, payload, length);
  });
}


bool RadarMqtt::reconnect() {
  String clientId =  WiFi.getHostname(); // name + '-' + String(random(0xffff), HEX);
  Serial.printf("Attempting MQTT connection... to %s name %s\n", server, clientId.c_str());
  if (client.connect(clientId.c_str())) {
    delay(2000);
    String cmnd_topic = String("cmnd/") + name + "/#";
    client.subscribe(cmnd_topic.c_str());
    Serial.printf("mqtt connected\n");

    StaticJsonDocument<200> doc;
    doc["version"] = 1;
//    doc["time"] = DateTime.toISOString();
    doc["hostname"] = WiFi.getHostname();
    doc["ip"] = WiFi.localIP().toString();
    String status_topic = "tele/" + name + "/init";
    String output;
    serializeJson(doc, output);
    client.publish(status_topic.c_str(), output.c_str());
    return true;
  } else {
    Serial.printf("failed to connect to %s port %d state %d\n", server, port, client.state());
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
  client.loop();
  copier->copy();
}
