#include "settings.h"


template<typename T>
bool getConfigOrDefault(DynamicJsonDocument& doc, const char* key, T& value) {
    T oldValue = value;
    if (doc.containsKey(key)) {
        value = doc[key].as<T>();
    }
#if 0
    else {
        Serial.printf("%s not found in config. Using default.\n", key);
    }
#endif
    return value != oldValue;
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
    } else {
        loadFromDocument(doc);
    }
    configFile.close();
}

std::vector<SettingsManager::SettingChange> SettingsManager::loadFromDocument(DynamicJsonDocument& doc) {
    std::vector<SettingChange> changes;
    if (getConfigOrDefault(doc, "volume", volume)) {
        changes.push_back(SettingChange::VolumeChanged);
    }
    getConfigOrDefault(doc, "mqtt_server", mqttServer);
    getConfigOrDefault(doc, "mqtt_port", mqttPort);
    getConfigOrDefault(doc, "sensor_name", sensorName);
    getConfigOrDefault(doc, "tracking", tracking);
    getConfigOrDefault(doc, "detection_timeout", detectionTimeout);
    getConfigOrDefault(doc, "tz", tz);
    return changes;
}

// Method to fill a DynamicJsonDocument with current settings
void SettingsManager::fillJsonDocument(DynamicJsonDocument& doc) {
    doc["mqtt_server"] = mqttServer;
    doc["mqtt_port"] = mqttPort;
    doc["sensor_name"] = sensorName;
    doc["tracking"] = tracking;
    doc["detection_timeout"] = detectionTimeout;
    doc["tz"] = tz;
    doc["volume"] = volume;
}

void SettingsManager::printSettings() {
    Serial.printf("MQTT Server: %s\n", mqttServer.c_str());
    Serial.printf("MQTT Port: %d\n", mqttPort);
    Serial.printf("Sensor Name: %s\n", sensorName.c_str());
    Serial.printf("Tracking: %d\n", tracking);
    Serial.printf("Detection Timeout: %d\n", detectionTimeout);
    Serial.printf("Timezone: %s\n", tz.c_str());
    Serial.printf("Volume: %d\n", volume);
}
