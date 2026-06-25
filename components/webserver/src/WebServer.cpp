#include "WebServer.h"
#include "JsonWrapper.h"
#include <ctime>
#include <vector>
#include <cstring>
#include <string>
#include "esp_random.h"
#include "esp_timer.h"

static const char* TAG = "WebServer";

QueueHandle_t WebServer::async_req_queue = nullptr;
SemaphoreHandle_t WebServer::worker_ready_count = nullptr;
TaskHandle_t WebServer::worker_handles[WebServer::MAX_ASYNC_REQUESTS] = {nullptr};

WebServer::WebServer(WebContext* context)
    : server(nullptr),
      webContext(context) {
    start_async_req_workers();
}

WebServer::~WebServer() {
    stop();
}

esp_err_t WebServer::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.server_port = 80;
    config.max_open_sockets = MAX_ASYNC_REQUESTS + 1;
    // Default is 8; derived servers (e.g. robofoc) register more than the base
    // /reset + /set_hostname + /healthz, so give the table generous headroom.
    config.max_uri_handlers = 16;

    ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);
    esp_err_t result = httpd_start(&server, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server!");
        return result;
    }

    httpd_uri_t resetWifiUri {
        .uri       = "/reset",
        .method    = HTTP_POST,
        .handler   = reset_wifi_handler,
        .user_ctx  = webContext
    };
    httpd_register_uri_handler(server, &resetWifiUri);

    httpd_uri_t setHostnameUri {
        .uri       = "/set_hostname",
        .method    = HTTP_POST,
        .handler   = set_hostname_handler,
        .user_ctx  = webContext
    };
    httpd_register_uri_handler(server, &setHostnameUri);

    httpd_uri_t healthzUri {
        .uri       = "/healthz",
        .method    = HTTP_GET,
        .handler   = healthz_handler,
        // Instead of storing 'webContext', we store 'this' so we can call
        // populate_healthz_fields(...) in the base or derived class.
        .user_ctx  = this
    };
    httpd_register_uri_handler(server, &healthzUri);

    return ESP_OK;
}

esp_err_t WebServer::stop() {
    if (server != nullptr) {
        esp_err_t result = httpd_stop(server);
        if (result == ESP_OK) {
            server = nullptr;
        }
        return result;
    }
    return ESP_OK;
}

void WebServer::start_async_req_workers() {
    worker_ready_count = xSemaphoreCreateCounting(MAX_ASYNC_REQUESTS, 0);
    if (!worker_ready_count) {
        ESP_LOGE(TAG, "Failed to create counting semaphore");
        return;
    }
    async_req_queue = xQueueCreate(1, sizeof(httpd_async_req_t));
    if (!async_req_queue) {
        ESP_LOGE(TAG, "Failed to create async request queue");
        vSemaphoreDelete(worker_ready_count);
        worker_ready_count = nullptr;
        return;
    }

    for (int i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        BaseType_t created = xTaskCreate(
            async_req_worker_task,
            "async_req_worker",
            ASYNC_WORKER_TASK_STACK_SIZE,
            nullptr,
            ASYNC_WORKER_TASK_PRIORITY,
            &worker_handles[i]
        );
        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to start async worker %d", i);
        }
    }
}

void WebServer::async_req_worker_task(void* arg) {
    ESP_LOGI(TAG, "Starting async request worker task");
    while (true) {
        xSemaphoreGive(worker_ready_count);

        httpd_async_req_t asyncReq;
        if (xQueueReceive(async_req_queue, &asyncReq, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Invoking %s", asyncReq.req->uri);
            asyncReq.handler(asyncReq.req);
            if (httpd_req_async_handler_complete(asyncReq.req) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to complete async request");
            }
        }
    }
    vTaskDelete(nullptr);
}

bool WebServer::is_on_async_worker_thread() {
    TaskHandle_t selfHandle = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        if (worker_handles[i] == selfHandle) {
            return true;
        }
    }
    return false;
}

esp_err_t WebServer::submit_async_req(httpd_req_t* req, httpd_req_handler_t handler) {
    httpd_req_t* copied = nullptr;
    esp_err_t error = httpd_req_async_handler_begin(req, &copied);
    if (error != ESP_OK) {
        return error;
    }

    httpd_async_req_t asyncReq;
    asyncReq.req = copied;
    asyncReq.handler = handler;

    if (xSemaphoreTake(worker_ready_count, 0) == pdFALSE) {
        httpd_req_async_handler_complete(copied);
        return ESP_FAIL;
    }
    if (xQueueSend(async_req_queue, &asyncReq, pdMS_TO_TICKS(100)) == pdFALSE) {
        httpd_req_async_handler_complete(copied);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// A helper to consistently return JSON error responses.
esp_err_t WebServer::sendJsonError(httpd_req_t* req, int statusCode, const std::string& message) {
    std::string statusString;
    switch (statusCode) {
        case 400: statusString = "400 Bad Request"; break;
        case 404: statusString = "404 Not Found"; break;
        case 500: statusString = "500 Internal Server Error"; break;
        default:
            statusString = std::to_string(statusCode);
            break;
    }
    httpd_resp_set_status(req, statusString.c_str());
    httpd_resp_set_type(req, "application/json");

    JsonWrapper json;
    json.AddItem("error", message);
    json.AddItem("statusCode", statusCode);

    std::string out = json.ToString();
    httpd_resp_sendstr(req, out.c_str());
    return ESP_FAIL;
}

esp_err_t WebServer::reset_wifi_handler(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    if (!ctx) {
        ESP_LOGE(TAG, "No valid WebContext");
        return sendJsonError(req, 500, "Missing context");
    }
    if (!ctx->wifiManager) {
        ESP_LOGE(TAG, "No valid wifiManager");
        return sendJsonError(req, 500, "Missing wifiManager");
    }

    ctx->wifiManager->clear();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"OK\"}");
    return ESP_OK;
}

esp_err_t WebServer::healthz_handler(httpd_req_t* req) {
    auto* server = static_cast<WebServer*>(req->user_ctx);
    if (!server) {
        return sendJsonError(req, 500, "Missing WebServer");
    }
    if (!server->webContext) {
        return sendJsonError(req, 500, "Missing WebContext");
    }

    uint64_t uptimeUs = esp_timer_get_time();
    uint32_t uptimeSec = static_cast<uint32_t>(uptimeUs / 1000000ULL);

    time_t now;
    time(&now);
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);

    char timeBuffer[30];
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%dT%H:%M:%S%z", &timeInfo);

    JsonWrapper json;
    json.AddItem("uptime", uptimeSec);
    json.AddItem("time", timeBuffer);

    // Derived classes can override this to add custom fields to 'json'
    server->populate_healthz_fields(server->webContext, json);

    std::string jsonStr = json.ToString();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, jsonStr.c_str());
    return ESP_OK;
}

esp_err_t WebServer::set_hostname_handler(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    if (!ctx) {
        ESP_LOGE(TAG, "No valid WebContext");
        return sendJsonError(req, 500, "Missing context");
    }
    if (!ctx->wifiManager) {
        ESP_LOGE(TAG, "No valid wifiManager");
        return sendJsonError(req, 500, "Missing wifiManager");
    }
    size_t contentLen = req->content_len;
    if (contentLen == 0) {
        return sendJsonError(req, 400, "Content-Length required");
    }

    std::string body;
    body.resize(contentLen);
    int ret = httpd_req_recv(req, &body[0], contentLen);
    if (ret <= 0) {
        return sendJsonError(req, 400, "Failed to read request body");
    }

    JsonWrapper json = JsonWrapper::Parse(body);
    if (json.Empty()) {
        return sendJsonError(req, 400, "Invalid JSON");
    }

	std::string host_name;
    if (!json.GetField("host_name", host_name, true)) {
		return sendJsonError(req, 400, "Missing or invalid 'host_name'");
	}

	ctx->wifiManager->configSetHostName(host_name);
	// reboot ?

    JsonWrapper response;
    response.AddItem("status", "OK");
	response.AddItem("host_name", host_name);
    std::string responseStr = response.ToString();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, responseStr.c_str());
    return ESP_OK;
}
