#include "sync_dp.h"
#include "telegraph_sync.h"
#include "sync_dp_response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

extern const char iot_ca_start[] __asm__("_binary_telegraph_iot_ca_crt_start");
static const char *TAG = "telegraph_dp";
static QueueHandle_t requests;
static telegraph_dp_content_callback_t content_callback;
static void *content_context;

typedef struct { char *json; } dp_request_t; /* NULL 表示 GET。 */

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        /* 按收到的字节输出，SSE 事件可能跨越多个回调，不假设分块边界。 */
        fwrite(event->data, 1, (size_t)event->data_len, stdout);
        fflush(stdout);
        if (event->user_data) (void)dp_response_feed(event->user_data, event->data, (size_t)event->data_len);
    }
    return ESP_OK;
}

static void on_content(const char *content, void *context)
{
    (void)context;
    if (content_callback) content_callback(false, content, content_context);
}

static void dp_worker(void *context)
{
    (void)context;
    dp_request_t request;
    while (xQueueReceive(requests, &request, portMAX_DELAY) == pdTRUE) {
        /* 只展示 POST 的回答正文；每个请求从空预览开始。 */
        if (request.json && content_callback) content_callback(true, NULL, content_context);
        /* 等待共享网络和校时，但离线时有界等待，不让队列无限积压。 */
        unsigned attempts = 0;
        while (!sync_dp_transport_ready() && attempts++ < 100) vTaskDelay(pdMS_TO_TICKS(200));
        if (!sync_dp_transport_ready()) {
            ESP_LOGW(TAG, "Network or clock unavailable; request discarded");
            free(request.json);
            continue;
        }
        dp_response_t *response = request.json ? dp_response_create(on_content, NULL) : NULL;
        if (request.json && !response) ESP_LOGE(TAG, "Preview allocation failed");
        const esp_http_client_config_t config = {
            .url = CONFIG_TELEGRAPH_SYNC_BASE_URL "/api/dp",
            .method = request.json ? HTTP_METHOD_POST : HTTP_METHOD_GET,
            .timeout_ms = 30000, .cert_pem = iot_ca_start,
            .event_handler = on_http_event, .user_data = response, .disable_auto_redirect = true,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = client ? ESP_OK : ESP_ERR_NO_MEM;
        if (err == ESP_OK && request.json) err = esp_http_client_set_header(client, "Content-Type", "application/json");
        if (err == ESP_OK && request.json) err = esp_http_client_set_post_field(client, request.json, (int)strlen(request.json));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "%s /api/dp response:", request.json ? "POST" : "GET");
            err = esp_http_client_perform(client);
            printf("\n");
        }
        if (err == ESP_OK) ESP_LOGI(TAG, "HTTP %d", esp_http_client_get_status_code(client));
        else ESP_LOGE(TAG, "Request failed: %s", esp_err_to_name(err));
        if (response && err == ESP_OK && !dp_response_finish(response))
            ESP_LOGW(TAG, "Response preview parsing failed or exceeded 16 KiB");
        if (client) esp_http_client_cleanup(client);
        dp_response_free(response);
        free(request.json);
    }
    vTaskDelete(NULL);
}

void telegraph_dp_set_content_callback(telegraph_dp_content_callback_t callback, void *context)
{
    content_callback = callback;
    content_context = context;
}

esp_err_t sync_dp_start(void)
{
    if (!CONFIG_TELEGRAPH_SYNC_BASE_URL[0]) return ESP_OK;
    requests = xQueueCreate(4, sizeof(dp_request_t));
    if (!requests) return ESP_ERR_NO_MEM;
    if (xTaskCreate(dp_worker, "telegraph_dp", 8192, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(requests);
        requests = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t enqueue(const char *json)
{
    if (!requests) return ESP_ERR_INVALID_STATE;
    if (json && strlen(json) > 4096) return ESP_ERR_INVALID_SIZE;
    dp_request_t request = { .json = json ? strdup(json) : NULL };
    if (json && !request.json) return ESP_ERR_NO_MEM;
    if (xQueueSend(requests, &request, 0) != pdTRUE) {
        free(request.json);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t telegraph_dp_post(const char *json)
{
    if (!json || !json[0]) return ESP_ERR_INVALID_ARG;
    return enqueue(json);
}

esp_err_t telegraph_dp_get_models(void)
{
    return enqueue(NULL);
}
