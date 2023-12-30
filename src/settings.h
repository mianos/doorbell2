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
            Serial.println("mqtt_server not found in config. Using default.");
        }

        if (doc.containsKey("mqtt_port")) {
            mqttPort = doc["mqtt_port"].as<int>();
        } else {
            Serial.println("mqtt_port not found in config. Using default.");
        }

        if (doc.containsKey("sensor_name")) {
            sensorName = doc["sensor_name"].as<String>();
        } else {
            Serial.println("sensor_name not found in config. Using default.");
        }

        if (doc.containsKey("tracking")) {
            tracking = doc["tracking"].as<bool>();  // Assuming tracking is a boolean
        } else {
            Serial.println("tracking not found in config. Using default.");
        }

        if (doc.containsKey("detection_timeout")) {
            detectionTimeout = doc["detection_timeout"].as<int>();
        } else {
            Serial.println("detection_timeout not found in config. Using default.");
        }

        configFile.close();
    }
};

