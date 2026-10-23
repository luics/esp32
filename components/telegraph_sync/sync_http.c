#include "sync_http.h"
#include "sync_tls_timing.h"
#include "sync_outbox.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_http_client.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <inttypes.h>

/* 仅内置私有 CA 公钥证书，信任配置由 ESP-TLS 解析并校验域名。 */
extern const char iot_ca_start[] __asm__("_binary_telegraph_iot_ca_crt_start");

typedef struct { char location[256]; bool headers_sent; } response_t;
/* 回调上下文与客户端同寿命，跨请求保留 TLS 连接；只由同步任务访问。 */
static response_t response;
static esp_http_client_handle_t client;
static bool connected;
static int64_t last_success;
#define PROBE_INTERVAL_US (25LL * 1000000)
#define STALE_CONNECTION_US (40LL * 1000000)

void sync_http_reset(void)
{
    if (client) esp_http_client_cleanup(client);
    client = NULL;
    connected = false;
    last_success = 0;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_CONNECTED) {
        connected = true;
        sync_tls_timing_connected();
    }
    if (event->event_id == HTTP_EVENT_DISCONNECTED) connected = false;
    if (event->event_id == HTTP_EVENT_HEADERS_SENT) response->headers_sent = true;
    if (event->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(event->header_key, "Location")) {
        snprintf(response->location, sizeof(response->location), "%s", event->header_value);
    }
    return ESP_OK;
}

static esp_err_t set_request_body(const char *json)
{
    const esp_err_t error = esp_http_client_set_post_field(client, json, json ? (int)strlen(json) : 0);
    /* IDF 6.1 已先清空 post_data/post_len，再删除 Content-Type。
     * HEAD 或重复解绑时该头可能不存在；仅在清空请求体时接受此幂等结果。 */
    return !json && error == ESP_ERR_NOT_FOUND ? ESP_OK : error;
}

void sync_http_disconnect(void)
{
    connected = false;
    last_success = 0;
    /* 仅关闭套接字，保留客户端中的会话票据，供重连时恢复 TLS 会话。 */
    if (client && (esp_http_client_close(client) != ESP_OK ||
                   set_request_body(NULL) != ESP_OK)) sync_http_reset();
}

static esp_err_t request(esp_http_client_method_t method, const char *url,
                         const char *json, int *status)
{
    memset(&response, 0, sizeof(response));
    esp_http_client_config_t config = {
        .url = url, .method = method,
        .timeout_ms = 10000, .cert_pem = iot_ca_start,
        .skip_cert_common_name_check = false,
        .event_handler = http_event, .user_data = &response, .disable_auto_redirect = true,
        .keep_alive_enable = true,
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
        .save_client_session = true,
#endif
    };
    esp_err_t err = ESP_OK;
    if (client) err = esp_http_client_set_url(client, url);
    else {
        client = esp_http_client_init(&config);
        if (!client) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) err = esp_http_client_set_method(client, method);
    if (err == ESP_OK && json) err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) err = set_request_body(json);
    if (err == ESP_OK) {
        sync_tls_timing_begin();
        err = esp_http_client_perform(client);
        sync_tls_timing_finish(err);
    }
    *status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    /* perform 读完响应后可复用连接，解绑调用方即将释放的 JSON。
     * 失败仅断开套接字，保留票据；错误的 POST 是否重试仍由下层策略判断。 */
    if (err != ESP_OK) sync_http_disconnect();
    else {
        last_success = esp_timer_get_time();
        if (set_request_body(NULL) != ESP_OK) sync_http_reset();
    }
    return err;
}

esp_err_t sync_http_prepare(void)
{
    const int64_t now = esp_timer_get_time();
    if (connected && now - last_success < PROBE_INTERVAL_US) return ESP_OK;
    /* 未及时探测的旧连接先关闭，保留会话票据；业务 POST 不用于试探连接存活。 */
    if (connected && now - last_success >= STALE_CONNECTION_US) sync_http_disconnect();
    int status;
    const esp_err_t error = request(HTTP_METHOD_HEAD, CONFIG_TELEGRAPH_SYNC_BASE_URL "/api/telegraph", NULL, &status);
    if (error == ESP_OK) {
        ESP_LOGI("telegraph_sync", "HTTPS preconnect: HTTP %d, %" PRId64 " ms", status,
                 (esp_timer_get_time() - now) / 1000);
    }
    return error;
}

sync_http_result_t sync_http_send(char id[37], const char *json, int *status_out, esp_err_t *error)
{
    const bool create = !id[0];
    char url[384];
    const int length = snprintf(url, sizeof(url), "%s/api/telegraph%s%s", CONFIG_TELEGRAPH_SYNC_BASE_URL,
                                create ? "" : "/", id);
    if (length <= 0 || (size_t)length >= sizeof(url)) {
        *status_out = 0;
        *error = ESP_ERR_INVALID_ARG;
        return SYNC_HTTP_BLOCK;
    }
    /* HEAD 可安全重试；失败时尚未提交任何业务内容，POST 不会被误判为结果不明。 */
    *error = sync_http_prepare();
    if (*error != ESP_OK) {
        *status_out = 0;
        return SYNC_HTTP_RETRY;
    }
    const esp_err_t err = request(create ? HTTP_METHOD_POST : HTTP_METHOD_PATCH, url, json, status_out);
    const int status = *status_out;
    const bool success = err == ESP_OK && (create ? status == 201 &&
        sync_location_id(response.location, CONFIG_TELEGRAPH_SYNC_BASE_URL, id) : status >= 200 && status < 300);
    *error = err;
    if (success) return SYNC_HTTP_ACK;
    /* 请求未发出可重试；POST 已发出但结果不明则阻止重复创建。 */
    if ((create && (response.headers_sent || err == ESP_OK) && status != 429) ||
        (err == ESP_OK && status >= 400 && status < 500 && status != 408 && status != 429)) return SYNC_HTTP_BLOCK;
    return SYNC_HTTP_RETRY;
}
