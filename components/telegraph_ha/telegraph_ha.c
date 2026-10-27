#include "telegraph_ha.h"
#include "sdkconfig.h"
#ifdef CONFIG_TELEGRAPH_HA_ENABLED
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "telegraph_sync.h"
#include "ha_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "telegraph_ha";
static QueueHandle_t snapshots, commands;
static esp_mqtt_client_handle_t client;
static atomic_bool connected, rediscover, subscribe_pending;
static char device_id[32], state_topic[80], command_topic[80], availability[80];
static ha_rx_t receiver;

static bool publish(const char *topic, const char *payload)
{
    /* 只有后台任务进入 MQTT outbox，断线只保留最新应用快照；队列上限防止耗尽 RAM。 */
    const int msg_id = esp_mqtt_client_enqueue(client, topic, payload, 0, 1, 1, true);
    if (msg_id >= 0) {
        /* 记录本次实际入队的上报内容，便于核对状态；入队不代表 Broker 已确认。 */
        ESP_LOGI(TAG, "MQTT queued: msg_id=%d, topic=%s, payload=%s", msg_id, topic, payload);
        return true;
    }
    ESP_LOGW(TAG, "MQTT outbox full; retry latest state/discovery");
    return false;
}

static bool publish_json(const char *topic, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    const bool ok = text && publish(topic, text);
    if (!text) ESP_LOGE(TAG, "JSON allocation failed; retry later");
    cJSON_free(text);
    cJSON_Delete(json);
    return ok;
}

static bool discovery(void)
{
    /* 独立命名空间，只发布本项目的灯实体，不改动电报机的 Discovery。 */
    char topic[128], unique[64];
    snprintf(topic, sizeof(topic), "homeassistant/light/%s_led/config", device_id);
    snprintf(unique, sizeof(unique), "%s_led", device_id);
    cJSON *json = cJSON_CreateObject();
    if (!json) return false;
    bool ok = cJSON_AddStringToObject(json, "name", "GPIO5 LED") &&
              cJSON_AddStringToObject(json, "unique_id", unique) &&
              cJSON_AddStringToObject(json, "state_topic", state_topic) &&
              cJSON_AddStringToObject(json, "command_topic", command_topic) &&
              cJSON_AddStringToObject(json, "availability_topic", availability) &&
              cJSON_AddBoolToObject(json, "retain", false) && cJSON_AddNumberToObject(json, "qos", 1);
    cJSON *device = cJSON_AddObjectToObject(json, "device");
    ok = ok && device && cJSON_AddStringToObject(device, "identifiers", device_id) &&
         cJSON_AddStringToObject(device, "name", "HA-Test") &&
         cJSON_AddStringToObject(device, "manufacturer", "DIY") &&
         cJSON_AddStringToObject(device, "model", "ESP32-C6");
    if (!ok) { cJSON_Delete(json); return false; }
    return publish_json(topic, json);
}

static bool report_state(bool on)
{
    return publish(state_topic, on ? "ON" : "OFF");
}

static void mqtt_event(void *context, esp_event_base_t base, int32_t id, void *data)
{
    (void)context;
    (void)base;
    esp_mqtt_event_handle_t event = data;
    if (id == MQTT_EVENT_CONNECTED) {
        /* 重连重新订阅并触发配置/真实状态上报，HA 重启也走同一恢复路径。 */
        memset(&receiver, 0, sizeof(receiver));
        atomic_store(&connected, true);
        atomic_store(&rediscover, true);
        atomic_store(&subscribe_pending, true);
        ESP_LOGI(TAG, "MQTT connected; device=%s", device_id);
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        atomic_store(&connected, false);
        xQueueReset(commands);
        memset(&receiver, 0, sizeof(receiver));
        ESP_LOGW(TAG, "MQTT disconnected; local operation continues");
    } else if (id == MQTT_EVENT_DATA) {
        const ha_message_t message = ha_receive(&receiver, command_topic, event->topic, event->topic_len,
            event->data, event->data_len, event->current_data_offset, event->total_data_len, event->retain);
        if (message == HA_REDISCOVER) atomic_store(&rediscover, true);
        if (message == HA_OFF || message == HA_ON) {
            const bool on = message == HA_ON;
            xQueueOverwrite(commands, &on);
        }
    } else if (id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "MQTT transport/broker error; automatic reconnect enabled");
    }
}

static void worker(void *context)
{
    (void)context;
    /* 网络仅由 telegraph_sync 初始化，取得 IP 即可 MQTT，HTTPS/SNTP 等待不阻塞本任务。 */
    while (!telegraph_sync_network_ready()) vTaskDelay(pdMS_TO_TICKS(500));
    const esp_mqtt_client_config_t config = {
        .broker.address.uri = CONFIG_TELEGRAPH_HA_BROKER_URI,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = device_id,
        .credentials.username = CONFIG_TELEGRAPH_HA_USERNAME[0] ? CONFIG_TELEGRAPH_HA_USERNAME : NULL,
        .credentials.authentication.password = CONFIG_TELEGRAPH_HA_PASSWORD[0] ? CONFIG_TELEGRAPH_HA_PASSWORD : NULL,
        .session.last_will = {.topic = availability, .msg = "offline", .qos = 1, .retain = 1},
        .session.keepalive = 30,
        .task.priority = 1,
        .outbox.limit = 8192,
    };
    client = esp_mqtt_client_init(&config);
    esp_err_t err = client ? esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event, NULL)
                          : ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT initialization failed: %s", esp_err_to_name(err));
        if (client) esp_mqtt_client_destroy(client);
        vTaskDelete(NULL);
        return;
    }
    bool state = false;
    bool have_state = false, dirty = false;
    while (true) {
        bool next;
        if (xQueueReceive(snapshots, &next, 0) == pdTRUE) {
            dirty |= !have_state || state != next;
            state = next;
            have_state = true;
        }
        if (atomic_load(&connected) && have_state) {
            if (atomic_exchange(&subscribe_pending, false)) {
                bool ok = esp_mqtt_client_subscribe(client, command_topic, 1) >= 0;
                ok = (esp_mqtt_client_subscribe(client, "homeassistant/status", 1) >= 0) && ok;
                if (!ok) atomic_store(&subscribe_pending, true);
            }
            if (atomic_exchange(&rediscover, false)) {
                /* HA birth 只重新发布，不重复订阅，避免 retained online 形成反馈循环。 */
                bool ok = discovery();
                if (ok) ok = report_state(state) && publish(availability, "online");
                if (!ok) atomic_store(&rediscover, true);
                dirty = !ok;
            } else if (dirty) {
                dirty = !report_state(state);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t telegraph_ha_start(void)
{
    if (snapshots) return ESP_ERR_INVALID_STATE;
    if (!CONFIG_TELEGRAPH_HA_BROKER_URI[0]) {
        ESP_LOGI(TAG, "No Broker configured; HA disabled");
        return ESP_OK;
    }
    if (!CONFIG_TELEGRAPH_SYNC_WIFI_SSID[0] ||
        (strncmp(CONFIG_TELEGRAPH_HA_BROKER_URI, "mqtt://", 7) &&
         strncmp(CONFIG_TELEGRAPH_HA_BROKER_URI, "mqtts://", 8)) ||
        strchr(CONFIG_TELEGRAPH_HA_BROKER_URI, '@')) return ESP_ERR_INVALID_ARG;
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(device_id, sizeof(device_id), "ha_test_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(state_topic, sizeof(state_topic), "luics/%s/state", device_id);
    snprintf(command_topic, sizeof(command_topic), "luics/%s/led/set", device_id);
    snprintf(availability, sizeof(availability), "luics/%s/availability", device_id);
    snapshots = xQueueCreate(1, sizeof(bool));
    commands = xQueueCreate(1, sizeof(bool));
    if (snapshots && commands && xTaskCreate(worker, "telegraph_ha", 6144, NULL, 1, NULL) == pdPASS) return ESP_OK;
    /* 初始化失败释放队列；HA 错误由入口记录，本地电报机继续运行。 */
    if (snapshots) vQueueDelete(snapshots);
    if (commands) vQueueDelete(commands);
    snapshots = commands = NULL;
    return ESP_ERR_NO_MEM;
}

void telegraph_ha_update(bool on)
{
    if (snapshots) xQueueOverwrite(snapshots, &on);
}

bool telegraph_ha_take_led(bool *on)
{
    return commands && xQueueReceive(commands, on, 0) == pdTRUE;
}
#else
esp_err_t telegraph_ha_start(void) { return ESP_OK; }
void telegraph_ha_update(bool on) { (void)on; }
bool telegraph_ha_take_led(bool *on) { (void)on; return false; }
#endif
