#include "telegraph_sync.h"
#include "sync_outbox.h"
#include "sync_http.h"
#include "sync_dp.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "telegraph_sync";
static sync_outbox_t outbox;
static SemaphoreHandle_t lock;
static EventGroupHandle_t network;
static bool enabled;
#define CONNECTED_BIT BIT0
#define TIME_READY_BIT BIT1

static void network_event(void *context, esp_event_base_t base, int32_t id, void *data)
{
    (void)context;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(network, CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(network, CONNECTED_BIT);
    }
}

static esp_err_t network_start(void)
{
    /* 不擦除 NVS；初始化错误只停止同步，电键和本地历史继续工作。 */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    if ((err = esp_netif_init()) != ESP_OK) return err;
    if ((err = esp_event_loop_create_default()) != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event, NULL)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL)) != ESP_OK) return err;
    wifi_config_t config = {0};
    const size_t ssid_len = strlen(CONFIG_TELEGRAPH_SYNC_WIFI_SSID);
    const size_t password_len = strlen(CONFIG_TELEGRAPH_SYNC_WIFI_PASSWORD);
    if (ssid_len > sizeof(config.sta.ssid) || password_len > sizeof(config.sta.password)) return ESP_ERR_INVALID_ARG;
    memcpy(config.sta.ssid, CONFIG_TELEGRAPH_SYNC_WIFI_SSID, ssid_len);
    memcpy(config.sta.password, CONFIG_TELEGRAPH_SYNC_WIFI_PASSWORD, password_len);
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    /* 证书校验使用内置私有 CA 与 SNTP 时间，不关闭主机名或证书校验。 */
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    return esp_netif_sntp_init(&time_config);
}

static void sync_worker(void *context)
{
    (void)context;
    esp_err_t err = network_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network initialization failed: %s; history remains local", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    bool time_ready = false;
    while (true) {
        if (!(xEventGroupGetBits(network) & CONNECTED_BIT)) {
            sync_http_disconnect();
            err = esp_wifi_connect();
            if (err != ESP_OK) ESP_LOGW(TAG, "Wi-Fi connect: %s", esp_err_to_name(err));
            xEventGroupWaitBits(network, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(5000));
            continue;
        }
        /* 空 API 地址用于仅接入 HA，Wi-Fi 仍由同一个任务维护。 */
        if (!CONFIG_TELEGRAPH_SYNC_BASE_URL[0]) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!time_ready) {
            time_ready = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) == ESP_OK;
            if (!time_ready) continue;
            xEventGroupSetBits(network, TIME_READY_BIT);
        }
        /* 提前握手并维护 HTTP 长连接；HEAD 失败不修改任何历史记录的同步状态。 */
        err = sync_http_prepare();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HTTPS preconnect failed: %s; retry in 5s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        /* 锁内只复制快照；DNS、TLS、重试等待全部在低优先级任务执行。 */
        xSemaphoreTake(lock, portMAX_DELAY);
        sync_record_t *record = outbox.head;
        while (record && (record->blocked || record->revision == record->sent_revision)) record = record->next;
        const size_t revision = record ? record->revision : 0;
        char id[37] = {0};
        char *json = NULL;
        if (record) {
            memcpy(id, record->id, sizeof(id));
            json = strdup(record->json);
        }
        xSemaphoreGive(lock);
        if (!record || !json) {
            if (record) ESP_LOGE(TAG, "Snapshot allocation failed; will retry");
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        const bool create = !id[0];
        int status;
        const TickType_t started = xTaskGetTickCount();
        const sync_http_result_t result = sync_http_send(id, json, &status, &err);
        const uint32_t duration_ms = (xTaskGetTickCount() - started) * portTICK_PERIOD_MS;
        const bool success = result == SYNC_HTTP_ACK;
        xSemaphoreTake(lock, portMAX_DELAY);
        if (success) {
            sync_record_ack(record, revision, id);
        } else if (result == SYNC_HTTP_BLOCK) {
            record->blocked = true;
        }
        xSemaphoreGive(lock);
        if (success) {
            ESP_LOGI(TAG, "%s %s: HTTP %d, %lu ms, payload=%s", create ? "POST" : "PATCH", id, status,
                     (unsigned long)duration_ms, json);
        } else {
            ESP_LOGE(TAG, "%s failed: %s, HTTP %d, %lu ms, payload=%s%s",
                     create ? "POST" : "PATCH", esp_err_to_name(err), status, (unsigned long)duration_ms, json,
                     record->blocked ? "; record blocked, local history retained" : "; retry in 5s");
        }
        /* 打印本次发送的完整快照，包含摩斯码、字符和标签，再释放以免读到后续版本。 */
        free(json);
        if (!success) vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t telegraph_sync_start(const char label[4])
{
    if (!label || strlen(label) != 3) return ESP_ERR_INVALID_ARG;
    if (!CONFIG_TELEGRAPH_SYNC_WIFI_SSID[0]) {
        ESP_LOGI(TAG, "No Wi-Fi SSID configured; API synchronization disabled");
        return ESP_OK;
    }
    if (lock) return ESP_ERR_INVALID_STATE;
    if (CONFIG_TELEGRAPH_SYNC_BASE_URL[0] &&
        (strncmp(CONFIG_TELEGRAPH_SYNC_BASE_URL, "https://", 8) ||
         CONFIG_TELEGRAPH_SYNC_BASE_URL[strlen(CONFIG_TELEGRAPH_SYNC_BASE_URL) - 1] == '/')) return ESP_ERR_INVALID_ARG;
    lock = xSemaphoreCreateMutex();
    network = xEventGroupCreate();
    if (!lock || !network) goto cleanup;
    memcpy(outbox.label, label, sizeof(outbox.label));
    /* TLS 大整数计算可能持续多个看门狗周期；与 IDLE 同级按时间片轮转，
     * 使其能持续喂狗，同时保持输入和显示任务优先。 */
    if (xTaskCreate(sync_worker, "telegraph_sync", 8192, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) goto cleanup;
    enabled = true;
    const esp_err_t dp_err = sync_dp_start();
    if (dp_err != ESP_OK) ESP_LOGE(TAG, "DeepSeek client disabled: %s", esp_err_to_name(dp_err));
    return ESP_OK;
cleanup:
    /* 启动失败释放已取得的同步资源，不留下半初始化队列。 */
    if (network) vEventGroupDelete(network);
    if (lock) vSemaphoreDelete(lock);
    network = NULL;
    lock = NULL;
    return ESP_ERR_NO_MEM;
}

esp_err_t telegraph_sync_history(const morse_t *morse)
{
    if (!morse) return ESP_ERR_INVALID_ARG;
    if (!enabled || !CONFIG_TELEGRAPH_SYNC_BASE_URL[0]) return ESP_OK;
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool ok = sync_outbox_capture(&outbox, morse);
    xSemaphoreGive(lock);
    return ok ? ESP_OK : ESP_ERR_NO_MEM;
}

/* 只读连接位；MQTT 不依赖 HTTPS 握手或校时完成。 */
bool telegraph_sync_network_ready(void)
{
    return network && (xEventGroupGetBits(network) & CONNECTED_BIT);
}

bool sync_dp_transport_ready(void)
{
    return network && (xEventGroupGetBits(network) & (CONNECTED_BIT | TIME_READY_BIT)) ==
                      (CONNECTED_BIT | TIME_READY_BIT);
}
