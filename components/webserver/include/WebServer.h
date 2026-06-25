#pragma once
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "WifiManager.h"
#include "JsonWrapper.h"

struct WebContext {
    WiFiManager* wifiManager;

    WebContext(WiFiManager* wifiManagerPointer)
        : wifiManager(wifiManagerPointer) {
    }
};

class WebServer {
public:
    WebServer(WebContext* context);
    virtual ~WebServer();

    virtual esp_err_t start();
    esp_err_t stop();

protected:
    virtual void populate_healthz_fields(WebContext *ctx, JsonWrapper& json) {}
    static esp_err_t healthz_handler(httpd_req_t* req);
	static esp_err_t sendJsonError(httpd_req_t* req, int statusCode, const std::string& message);

    httpd_handle_t server;
    WebContext* webContext;

private:
    static constexpr int ASYNC_WORKER_TASK_PRIORITY = 5;
    static constexpr int ASYNC_WORKER_TASK_STACK_SIZE = 4096;
    static constexpr int MAX_ASYNC_REQUESTS = 5;

    using httpd_req_handler_t = esp_err_t (*)(httpd_req_t* req);

    struct httpd_async_req_t {
        httpd_req_t* req;
        httpd_req_handler_t handler;
    };

    static QueueHandle_t async_req_queue;
    static SemaphoreHandle_t worker_ready_count;
    static TaskHandle_t worker_handles[MAX_ASYNC_REQUESTS];

    static void async_req_worker_task(void* arg);
    static bool is_on_async_worker_thread();
    static esp_err_t submit_async_req(httpd_req_t* req, httpd_req_handler_t handler);
    static void start_async_req_workers();

    static esp_err_t reset_wifi_handler(httpd_req_t* req);
    static esp_err_t set_hostname_handler(httpd_req_t* req);
};

