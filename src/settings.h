#pragma once
#include <SPIFFS.h>
#include <ArduinoJson.h>

struct SettingsManager {
    String mqttServer = "mqtt2.mianos.com";
    int mqttPort = 1883;
    String sensorName = "doorbell2";
    int tracking = 0;
    int detectionTimeout = 10000;
    String tz = "AEST-10AEDT,M10.1.0,M4.1.0/3";
    int volume = 100;

    SettingsManager(); // Constructor declaration
};

// Function declarations
void getConfigOrDefault(DynamicJsonDocument& doc, const char* key, String& value);
void getConfigOrDefault(DynamicJsonDocument& doc, const char* key, int& value);
void getConfigOrDefault(DynamicJsonDocument& doc, const char* key, bool& value);
