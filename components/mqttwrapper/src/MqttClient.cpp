#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "esp_log.h"

#include "MqttClient.h"

static const char* TAG = "MqttClient";

MqttClient::MqttClient(esp_mqtt_client_config_t& mqtt_cfg, std::string sensorName)
        : sensorName(sensorName) {
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client,
								static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
								mqtt_event_handler,
								this);
}

MqttClient::~MqttClient() {
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
}

void MqttClient::start() {
    esp_mqtt_client_start(client);
}

void MqttClient::publish(std::string topic, std::string data, int qos) {
    // esp_mqtt_client_publish is internally thread-safe, so concurrent callers
    // (PID timer, sensor loop, main task) don't need external locking here.
    if (connected_.load()) {
        esp_mqtt_client_publish(client, topic.c_str(), data.c_str(), 0, qos, 0);
    } else {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (messageQueue.size() >= kMaxQueued) {
            messageQueue.pop_front();  // drop oldest; telemetry favours fresh data
        }
        messageQueue.emplace_back(std::move(topic), std::move(data));
    }
}

void MqttClient::flushMessageQueue() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!messageQueue.empty()) {
        const auto& msg = messageQueue.front();
        // Flush buffered messages at QoS 0 — they are already stale, so there is
        // no value in retransmitting them.
        esp_mqtt_client_publish(client, msg.first.c_str(), msg.second.c_str(), 0, 0, 0);
        messageQueue.pop_front();
    }
}

void MqttClient::subscribe(std::string topic) {
    // Check if the topic is already in the vector to avoid duplicates
    if (std::find(subscriptions.begin(), subscriptions.end(), topic) == subscriptions.end()) {
        subscriptions.push_back(topic);  // Add to subscriptions if not already present
    }
    // Perform subscription immediately only if already connected; otherwise it
    // happens in resubscribe() on the next connect.
    if (connected_.load()) {
        esp_mqtt_client_subscribe(client, topic.c_str(), 0);
    }
}

void MqttClient::resubscribe() {
    for (const auto& topic : subscriptions) {
        esp_mqtt_client_subscribe(client, topic.c_str(), 0);
    }
}

void MqttClient::registerHandler(const std::string topic,
							     const std::regex pattern,
								 HandlerFunc handler, void* context) {
	subscribe(topic);
	bindings.push_back({topic, pattern, handler, context});
}


void MqttClient::dispatchEvent(MqttClient* client, const std::string& topic, const JsonWrapper& data) {
	//ESP_LOGI(TAG, "topic in '%s'", topic.c_str());
    for (const auto& binding : client->bindings) {
        if (std::regex_match(topic, binding.matchPattern)) {
            if (binding.handler) {
                binding.handler(client, topic, data, binding.context);
                return; // Assuming only one handler per topic pattern
            }
        }
    }
    ESP_LOGW(TAG, "Unhandled topic: %s", topic.c_str());
}


void MqttClient::mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* clientInstance = static_cast<MqttClient*>(handler_args); // Cast to MqttClient*
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data); // Cast event_data to esp_mqtt_event_handle_t

    // Check the type of MQTT event
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        clientInstance->connected_.store(true);
        clientInstance->resubscribe();
		clientInstance->flushMessageQueue();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        clientInstance->connected_.store(false);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
       //  ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA: {
			std::string topicStr(event->topic, event->topic_len);
			std::string payloadStr(event->data, event->data_len);

			auto jsonPayload = JsonWrapper::Parse(payloadStr);
			if (!jsonPayload.Empty()) {
				dispatchEvent(clientInstance, topicStr, jsonPayload);
			} else {
				const char* error_ptr = cJSON_GetErrorPtr();
				if (error_ptr != nullptr) {
					ESP_LOGE(TAG, "Error parsing JSON: %s", error_ptr);
				}
			}
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other MQTT Event ID: %d", event->event_id);
        break;
    }
}

void MqttClient::wait_for_connection() {
    while (!connected_.load()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "Connected");
}
