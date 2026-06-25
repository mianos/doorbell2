#include "Settings.h"

#include "esp_log.h"

static const char* TAG = "settings";

Settings::Settings(NvsStorageManager& nvs, const std::string& key)
    : nvs_(nvs), key_(key) {
    std::string raw;
    if (nvs_.retrieve(key_, raw) && !raw.empty()) {
        JsonWrapper doc = JsonWrapper::Parse(raw);
        if (!doc.Empty()) {
            loadFromJson(doc);
        } else {
            ESP_LOGW(TAG, "stored config unparseable, using defaults");
        }
    } else {
        ESP_LOGI(TAG, "no stored config, using defaults");
    }
}

std::vector<Settings::Change> Settings::loadFromJson(const JsonWrapper& doc) {
    std::vector<Change> changes;

    int oldVolume = volume;
    doc.GetField("volume", volume);
    if (volume != oldVolume) changes.push_back(Change::VolumeChanged);

    doc.GetField("mqtt_server", mqttServer);
    doc.GetField("mqtt_port", mqttPort);
    doc.GetField("sensor_name", sensorName);
    doc.GetField("tracking", tracking);
    doc.GetField("presence", presence);
    doc.GetField("detection_timeout", detectionTimeout);
    doc.GetField("tz", tz);
    return changes;
}

JsonWrapper Settings::toJson() const {
    JsonWrapper d;
    d.AddItem("mqtt_server", mqttServer);
    d.AddItem("mqtt_port", mqttPort);
    d.AddItem("sensor_name", sensorName);
    d.AddItem("tracking", tracking);
    d.AddItem("presence", presence);
    d.AddItem("detection_timeout", detectionTimeout);
    d.AddItem("tz", tz);
    d.AddItem("volume", volume);
    return d;
}

void Settings::save() const {
    if (!nvs_.store(key_, toJson().ToString())) {
        ESP_LOGE(TAG, "failed to persist settings");
    }
}

void Settings::resetToDefaults() {
    // Construct a throwaway Settings against an NVS key that is never written,
    // so it keeps the compiled-in member defaults (single source of truth in
    // Settings.h) instead of duplicating them here.
    Settings defaults(nvs_, "__defaults_probe__");
    mqttServer       = defaults.mqttServer;
    mqttPort         = defaults.mqttPort;
    sensorName       = defaults.sensorName;
    tracking         = defaults.tracking;
    presence         = defaults.presence;
    detectionTimeout = defaults.detectionTimeout;
    tz               = defaults.tz;
    volume           = defaults.volume;
}

void Settings::log() const {
    ESP_LOGI(TAG, "mqtt=%s:%d name=%s tracking=%d presence=%d timeout=%d vol=%d tz=%s",
             mqttServer.c_str(), mqttPort, sensorName.c_str(), tracking, presence,
             detectionTimeout, volume, tz.c_str());
}
