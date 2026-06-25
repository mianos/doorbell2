#pragma once

#include <string>
#include <vector>

#include "NvsStorageManager.h"
#include "JsonWrapper.h"

// Persistent device configuration. Mirrors the doorbell2 SettingsManager but
// stores a JSON blob in NVS (via NvsStorageManager) instead of a SPIFFS file.
// Missing/garbage config falls back to the defaults below (fail-fast + log).
class Settings {
public:
    std::string mqttServer       = "mqtt2.mianos.com";
    int         mqttPort         = 1883;
    std::string sensorName       = "doorbell3";
    int         tracking         = 0;        // tracking publish interval (ms); 0 = off
    int         presence         = 10000;    // presence publish interval (ms)
    int         detectionTimeout = 60000;    // ms of no motion before "cleared"
    std::string tz               = "AEST-10AEDT,M10.1.0,M4.1.0/3";
    int         volume           = 100;      // 0..100

    enum class Change { VolumeChanged };

    explicit Settings(NvsStorageManager& nvs, const std::string& key = "config");

    // Apply fields present in `doc`; returns which notable settings changed.
    std::vector<Change> loadFromJson(const JsonWrapper& doc);

    JsonWrapper toJson() const;   // full settings as JSON
    void        save() const;     // persist to NVS
    void        resetToDefaults();// restore every field to its compiled-in default
    void        log() const;

private:
    NvsStorageManager& nvs_;
    std::string        key_;
};
