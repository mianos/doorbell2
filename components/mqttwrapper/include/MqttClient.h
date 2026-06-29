#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>

#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"

#include "JsonWrapper.h"

using HandlerFunc = std::function<esp_err_t(class MqttClient*, const std::string&, const JsonWrapper&, void*)>;

struct HandlerBinding {
    std::string subscriptionTopic;
    std::regex matchPattern;
    HandlerFunc handler;
    void* context;  // Use void* for context
};

class MqttClient {
public:
	std::string sensorName;

	MqttClient(esp_mqtt_client_config_t& mqtt_cfg, std::string sensorName);
    ~MqttClient();

    void start();
    void wait_for_connection();
    // Telemetry defaults to QoS 0 (fire-and-forget): periodic data is superseded
    // by the next sample, so retransmitting a stale value is worse than dropping
    // it. Pass qos=1 only for messages that must not be lost.
    void publish(std::string topic, std::string data, int qos = 0);
    void subscribe(std::string topic);

	void registerHandler(const std::string topic, const std::regex pattern, HandlerFunc handler, void* context);

private:
    // While disconnected we buffer a *bounded* number of messages and drop the
    // oldest beyond that, so a reconnect can't dump a large backlog of stale
    // telemetry. Small on purpose.
    static constexpr size_t kMaxQueued = 32;

    std::atomic<bool> connected_{false};
    esp_mqtt_client_handle_t client;
    std::vector<std::string> subscriptions;
    std::mutex queueMutex_;
    std::deque<std::pair<std::string, std::string>> messageQueue;

    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    void resubscribe();
    void flushMessageQueue();

	std::vector<HandlerBinding> bindings;
	static void dispatchEvent(MqttClient* client, const std::string& topic, const JsonWrapper& data);
};
