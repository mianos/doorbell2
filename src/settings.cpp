#include "settings.h"

// Function for String
void getConfigOrDefault(DynamicJsonDocument& doc, const char* key, String& value) {
    if (doc.containsKey(key)) {
        value = doc[key].as<String>();
    } else {
        Serial.printf("%s not found in config. Using default: %s\n", key, value.c_str());
    }
}

// Function for int
void getConfigOrDefault(DynamicJsonDocument& doc, const char* key, int& value) {
    if (doc.containsKey(key)) {
        value = doc[key].as<int>();
    } else {
        Serial.printf("%s not found in config. Using default: %d\n", key, value);
    }
}

// Function for bool
void getConfigOrDefault(DynamicJsonDocument& doc, const char* key, bool& value) {
    if (doc.containsKey(key)) {
        value = doc[key].as<bool>();
    } else {
        Serial.printf("%s not found in config. Using default: %s\n", key, value ? "true" : "false");
    }
}

SettingsManager::SettingsManager() {
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

    getConfigOrDefault(doc, "mqtt_server", mqttServer);
    getConfigOrDefault(doc, "mqtt_port", mqttPort);
    getConfigOrDefault(doc, "sensor_name", sensorName);
    getConfigOrDefault(doc, "tracking", tracking);
    getConfigOrDefault(doc, "detection_timeout", detectionTimeout);
    getConfigOrDefault(doc, "tz", tz);
    getConfigOrDefault(doc, "volume", volume);

    configFile.close();
}
