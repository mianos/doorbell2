#pragma once
#include <SPIFFS.h>
#include <ArduinoJson.h>

struct SettingsManager {
    String mqttServer = "mqtt2.mianos.com";
    int mqttPort = 1883;
    String sensorName = "doorbell2";
    bool tracking = false;
    int detectionTimeout = 10000;

    SettingsManager() {
        if (!SPIFFS.begin(true)) {
            Serial.printf("An error occurred while mounting SPIFFS\n");
            return;
        }

        File configFile = SPIFFS.open("/config.json", "r");
        if (!configFile) {
            Serial.printf("Failed to open config file. Loading default settings\n");
            return;
        }

        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, configFile);
        if (error) {
            Serial.println("Failed to deserialize config file. Loading default settings\n");
            configFile.close();
            return;
        }

        if (doc.containsKey("mqtt_server")) {
            mqttServer = doc["mqtt_server"].as<String>();
        } else {
            Serial.printf("mqtt_server not found in config. Using default: %s\n", mqttServer.c_str());
        }

        if (doc.containsKey("mqtt_port")) {
            mqttPort = doc["mqtt_port"].as<int>();
        } else {
            Serial.printf("mqtt_port not found in config. Using default: %d\n", mqttPort);
        }

        if (doc.containsKey("sensor_name")) {
            sensorName = doc["sensor_name"].as<String>();
        } else {
            Serial.printf("sensor_name not found in config. Using default: %s\n", sensorName.c_str());
        }

        if (doc.containsKey("tracking")) {
            tracking = doc["tracking"].as<bool>();  // Assuming tracking is a boolean
        } else {
            Serial.printf("tracking not found in config. Using default: %s\n", tracking ? "true" : "false");
        }

        if (doc.containsKey("detection_timeout")) {
            detectionTimeout = doc["detection_timeout"].as<int>();
        } else {
            Serial.printf("detection_timeout not found in config. Using default: %d\n", detectionTimeout);
        }

        configFile.close();
    }
};
