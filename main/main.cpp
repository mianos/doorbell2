// doorbell3 — ESP-IDF port of the doorbell2 Arduino app.
//
// Plays MP3 chimes (queued, never dropped) on MQTT command, reports LD2450
// radar presence/tracking over MQTT, persists settings in NVS, exposes a health
// endpoint, and keeps NTP time. Audio runs in its own task so a chime can never
// block the doorbell.

#include <string>
#include <regex>
#include <ctime>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"

#include "NvsStorageManager.h"
#include "WifiManager.h"
#include "Settings.h"
#include "AudioPlayer.h"
#include "MqttClient.h"
#include "WebServer.h"
#include "DoorbellWebServer.h"
#include "JsonWrapper.h"
#include "Ld2450.h"
#include "LocalEP.h"
#include "RadarPublisher.h"

static const char* TAG = "doorbell3";

namespace {

struct App {
    Settings*    settings;
    AudioPlayer* player;
    MqttClient*  mqtt;
    WiFiManager* wifi;
};

std::string uptimeString() {
    uint32_t seconds = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t days = seconds / 86400; seconds %= 86400;
    uint32_t hours = seconds / 3600; seconds %= 3600;
    uint32_t minutes = seconds / 60;
    return std::to_string(days) + "d " + std::to_string(hours) + "h " +
           std::to_string(minutes) + "m";
}

std::string localIp() {
    char buf[16] = "0.0.0.0";
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        esp_ip4addr_ntoa(&ip.ip, buf, sizeof(buf));
    }
    return std::string(buf);
}

// Radar event sink: presence/tracking over MQTT (publish queues while offline).
class MqttRadarPublisher : public RadarPublisher {
public:
    MqttRadarPublisher(MqttClient& mqtt, Settings& settings)
        : mqtt_(mqtt), settings_(settings) {}

    void publishPresence(bool entry, const Value* v) override {
        JsonWrapper d;
        d.AddItem("entry", entry);
        d.AddTime();
        if (entry && v) v->toJson(d);
        mqtt_.publish("tele/" + settings_.sensorName + "/presence", d.ToString());
    }

    void publishTracking(const Value* v) override {
        JsonWrapper d;
        d.AddTime();
        if (v) v->toJson(d);
        mqtt_.publish("tele/" + settings_.sensorName + "/tracking", d.ToString());
    }

private:
    MqttClient& mqtt_;
    Settings&   settings_;
};

// ---- MQTT command handlers (cmnd/<name>/<cmd>) ----

esp_err_t handlePlay(MqttClient*, const std::string&, const JsonWrapper& d, void* ctx) {
    auto* app = static_cast<App*>(ctx);
    std::string url;
    if (d.GetField("url", url)) {
        int volume = -1;
        d.GetField("volume", volume);
        app->player->enqueue(url, volume);
    } else {
        ESP_LOGW(TAG, "play command without url");
    }
    return ESP_OK;
}

esp_err_t handleSettings(MqttClient*, const std::string&, const JsonWrapper& d, void* ctx) {
    auto* app = static_cast<App*>(ctx);
    auto changes = app->settings->loadFromJson(d);
    app->settings->save();
    for (auto c : changes) {
        if (c == Settings::Change::VolumeChanged) {
            app->player->setDefaultVolume(app->settings->volume);
        }
    }
    app->settings->log();
    return ESP_OK;
}

esp_err_t handleRestart(MqttClient*, const std::string&, const JsonWrapper&, void*) {
    ESP_LOGW(TAG, "restart requested");
    esp_restart();
    return ESP_OK;
}

esp_err_t handleReprovision(MqttClient*, const std::string&, const JsonWrapper&, void* ctx) {
    ESP_LOGW(TAG, "reprovision requested");
    static_cast<App*>(ctx)->wifi->clear();  // clears Wi-Fi creds and restarts
    return ESP_OK;
}

// --- OTA rollback verification ---
//
// A freshly-OTA'd image boots in PENDING_VERIFY: the bootloader will roll it
// back to the previous slot on the next reset unless the running app declares
// itself good. We declare it good only after the device actually gets back on
// the network, so an image that boots but can't reach Wi-Fi is rolled back
// instead of stranding an unreachable (un-OTA-able) device.
//
// Only PENDING_VERIFY images are touched: a wired first-flash is UNDEFINED, so
// this never interferes with normal/bench boots, and a real OTA always has a
// valid previous slot to roll back to.
constexpr int OTA_VERIFY_TIMEOUT_MS = 120000;  // creds survive an OTA, so reconnect is seconds; this is slack
SemaphoreHandle_t s_got_ip = nullptr;

void onGotIp(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && s_got_ip) {
        xSemaphoreGive(s_got_ip);
    }
}

void otaVerifyTask(void*) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "OTA: image pending verify; waiting up to %ds for connectivity",
                 OTA_VERIFY_TIMEOUT_MS / 1000);
        if (xSemaphoreTake(s_got_ip, pdMS_TO_TICKS(OTA_VERIFY_TIMEOUT_MS)) == pdTRUE) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "OTA: connectivity confirmed, image marked valid (rollback cancelled)");
        } else {
            ESP_LOGE(TAG, "OTA: no IP within timeout; rolling back to previous image");
            esp_ota_mark_app_invalid_rollback_and_reboot();  // reboots on success
            // Only reached if rollback was impossible (no valid previous slot).
            // Keep running this image rather than reboot-looping in PENDING_VERIFY.
            ESP_LOGE(TAG, "OTA: rollback not possible; keeping current image");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    vTaskDelete(nullptr);
}

void radarTask(void* arg) {
    auto* radar = static_cast<RadarSensor*>(arg);
    for (;;) {
        radar->process();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void telemetryTask(void* arg) {
    auto* app = static_cast<App*>(arg);
    const std::string base = "tele/" + app->settings->sensorName + "/";

    // Wait (bounded) for SNTP so the init timestamp is real.
    for (int i = 0; i < 20 && time(nullptr) < 1700000000; ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Build identity from the app descriptor embedded in the image (version is
    // git describe, e.g. "9bbad4c-dirty"), so a deploy is verifiable from MQTT.
    const esp_app_desc_t* desc = esp_app_get_description();
    JsonWrapper init;
    init.AddItem("version", 3);
    init.AddItem("build", std::string(desc->version));
    init.AddItem("built", std::string(desc->date) + " " + std::string(desc->time));
    init.AddTime();
    init.AddItem("hostname", app->settings->sensorName);
    init.AddItem("ip", localIp());
    app->mqtt->publish(base + "init", init.ToString());

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        JsonWrapper d;
        d.AddTime();
        d.AddItem("uptime", uptimeString());
        d.AddItem("heap_free", (int)esp_get_free_heap_size());
        d.AddItem("heap_min_free", (int)esp_get_minimum_free_heap_size());
        app->mqtt->publish(base + "status", d.ToString());
    }
}

}  // namespace

extern "C" void app_main(void) {
    static NvsStorageManager nvs;
    static Settings settings(nvs);
    settings.log();

    // Created before Wi-Fi starts so the got-IP handler can signal it. Binary
    // semaphore retains the token if IP arrives before the verify task waits.
    s_got_ip = xSemaphoreCreateBinary();

    // Wi-Fi: provisions (ESP-Touch v2) on first boot, else reconnects. The
    // onGotIp handler feeds OTA rollback verification.
    static WiFiManager wifi(nvs, onGotIp, nullptr);
    std::string host = settings.sensorName;
    wifi.configSetHostName(host);

    // Modem power save (the IDF default) sleeps the radio between DTIM beacons,
    // stalling HTTP for 100-300 ms at a time — audible as chopped audio. This
    // device is mains-powered, so keep the radio awake.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // NTP time in the configured timezone.
    setenv("TZ", settings.tz.c_str(), 1);
    tzset();
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);

    // Audio player (its own task + queue).
    static AudioPlayer player;
    player.setDefaultVolume(settings.volume);
    player.start();

    // MQTT (anonymous, plain mqtt://, with a Last-Will on the status topic).
    static std::string uri = "mqtt://" + settings.mqttServer + ":" + std::to_string(settings.mqttPort);
    static std::string statusTopic = "tele/" + settings.sensorName + "/status";
    static std::string lwt = "{\"status\":\"offline\"}";
    static esp_mqtt_client_config_t mcfg = {};
    mcfg.broker.address.uri        = uri.c_str();
    mcfg.credentials.client_id     = settings.sensorName.c_str();
    mcfg.session.last_will.topic   = statusTopic.c_str();
    mcfg.session.last_will.msg     = lwt.c_str();
    mcfg.session.last_will.msg_len = (int)lwt.size();
    mcfg.session.last_will.qos     = 1;
    static MqttClient mqtt(mcfg, settings.sensorName);

    static App app{ &settings, &player, &mqtt, &wifi };

    const std::string b = "cmnd/" + settings.sensorName + "/";
    mqtt.registerHandler(b + "play",        std::regex(b + "play"),        handlePlay,        &app);
    mqtt.registerHandler(b + "settings",    std::regex(b + "settings"),    handleSettings,    &app);
    mqtt.registerHandler(b + "restart",     std::regex(b + "restart"),     handleRestart,     &app);
    mqtt.registerHandler(b + "reprovision", std::regex(b + "reprovision"), handleReprovision, &app);
    mqtt.start();

    // Web server: base /healthz, /reset, /set_hostname plus OTA (/firmware) and
    // /config, /config/reset.
    static WebContext webctx(&wifi);
    static DoorbellWebServer web(&webctx, settings, player);
    web.start();

    // OTA: verify a freshly-OTA'd image only once it's back online; roll back if
    // it can't connect (see otaVerifyTask). No-op on a normal/wired boot.
    xTaskCreate(otaVerifyTask, "ota_verify", 4096, nullptr, 4, nullptr);

    // Radar -> presence/tracking over MQTT.
    static MqttRadarPublisher publisher(mqtt, settings);
    static LocalEP lep(&publisher, &settings);
    static Ld2450 radar(&lep, &settings);

    xTaskCreate(radarTask, "radar", 4096, &radar, 4, nullptr);
    xTaskCreate(telemetryTask, "telemetry", 4096, &app, 4, nullptr);

    ESP_LOGI(TAG, "doorbell3 started");
}
