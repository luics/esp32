"""使用 HTTP 替身验证真实同步传输代码，不向线上 API 写入测试记录。"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
int64_t esp_timer_get_time(void);
static inline void test_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
#define ESP_LOGI test_log
typedef int esp_err_t;
enum {ESP_OK=0, ESP_ERR_NO_MEM=-1, ESP_ERR_TIMEOUT=-2, ESP_ERR_INVALID_ARG=-3, ESP_ERR_NOT_FOUND=-4};
enum {HTTP_EVENT_DISCONNECTED, HTTP_EVENT_ON_CONNECTED, HTTP_EVENT_HEADERS_SENT, HTTP_EVENT_ON_HEADER, HTTP_METHOD_POST, HTTP_METHOD_PATCH, HTTP_METHOD_HEAD};
typedef int esp_http_client_method_t;
typedef struct {int event_id; void *user_data; char *header_key; char *header_value;} esp_http_client_event_t;
typedef struct {
    const char *url;
    int method, timeout_ms;
    const char *cert_pem;
    bool skip_cert_common_name_check;
    esp_err_t (*crt_bundle_attach)(void *);
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
    bool disable_auto_redirect;
    bool keep_alive_enable;
    bool save_client_session;
} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
typedef struct { const uint16_t *groups; bool bundle_attached; } mbedtls_ssl_config;
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1 0x0017
void mbedtls_ssl_conf_groups(mbedtls_ssl_config *, const uint16_t *);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(void *, const char *, const char *);
esp_err_t esp_http_client_set_post_field(void *, const char *, int);
esp_err_t esp_http_client_set_url(void *, const char *);
esp_err_t esp_http_client_set_method(void *, int);
esp_err_t esp_http_client_perform(void *);
int esp_http_client_get_status_code(void *);
esp_err_t esp_http_client_cleanup(void *);
esp_err_t esp_http_client_close(void *);
esp_err_t esp_crt_bundle_attach(void *);
#define CONFIG_TELEGRAPH_SYNC_BASE_URL "https://telegraph-iot.bryanxu.top"
#define CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS 1
'''
HARNESS = r'''
#include "test_sdk.h"
#include "sync_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
const char iot_ca_start[] __asm__("_binary_telegraph_iot_ca_crt_start") = "TEST PRIVATE CA PUBLIC CERT";
void sync_tls_timing_begin(void) {}
void sync_tls_timing_configured(const void *config) {(void)config;}
void sync_tls_timing_connected(void) {}
void sync_tls_timing_finish(esp_err_t error) {(void)error;}
static esp_http_client_config_t config;
static int status=201, method=HTTP_METHOD_POST, cleanup_count, init_count, close_count;
static esp_err_t outcome=ESP_OK, head_outcome=ESP_OK;
static int actual_method, head_count, business_count;
static int64_t now=1000000;
int64_t esp_timer_get_time(void) {return now;}
static bool sent=true, has_content_type;
static unsigned missing_header_clears;
static esp_err_t json_error=ESP_OK, clear_error=ESP_OK;
static char *location="/api/telegraph/01234567-89ab-cdef-0123-456789abcdef";
static const char *body="{\"morse\":\".... ..\",\"content\":\"HI\",\"label\":\"A09\"}";
static const char *retained_body;
void mbedtls_ssl_conf_groups(mbedtls_ssl_config *p,const uint16_t *groups){p->groups=groups;}
esp_err_t esp_crt_bundle_attach(void *p){((mbedtls_ssl_config *)p)->bundle_attached=true;return ESP_OK;}
void *esp_http_client_init(const esp_http_client_config_t *c){
    ++init_count;
    config=*c;
    assert(c->method==HTTP_METHOD_HEAD && c->timeout_ms==10000 && c->disable_auto_redirect && c->keep_alive_enable);
    assert(!c->crt_bundle_attach && c->save_client_session);
    assert(c->cert_pem==iot_ca_start && !c->skip_cert_common_name_check);
    assert(!strcmp(c->url, method!=HTTP_METHOD_PATCH ? "https://telegraph-iot.bryanxu.top/api/telegraph" :
        "https://telegraph-iot.bryanxu.top/api/telegraph/01234567-89ab-cdef-0123-456789abcdef"));
    return (void*)1;
}
esp_err_t esp_http_client_set_url(void *p,const char *url){(void)p;assert(!strcmp(url, "https://telegraph-iot.bryanxu.top/api/telegraph") || !strcmp(url, method!=HTTP_METHOD_PATCH ?
    "https://telegraph-iot.bryanxu.top/api/telegraph" : "https://telegraph-iot.bryanxu.top/api/telegraph/01234567-89ab-cdef-0123-456789abcdef"));return ESP_OK;}
esp_err_t esp_http_client_set_method(void *p,int m){(void)p;assert(m==method || m==HTTP_METHOD_HEAD);actual_method=m;return ESP_OK;}
esp_err_t esp_http_client_set_header(void *p,const char *key,const char *value){(void)p;assert(!strcmp(key,"Content-Type")&&!strcmp(value,"application/json"));has_content_type=true;return ESP_OK;}
esp_err_t esp_http_client_set_post_field(void *p,const char *json,int length){(void)p;
    if(json)assert(length==(int)strlen(body)&&!strcmp(body,json));else assert(length==0);
    retained_body=json;
    if(json && json_error!=ESP_OK)return json_error;
    if(!json){
        if(clear_error!=ESP_OK)return clear_error;
        /* IDF 6.1 先清空 post_data，再删除 Content-Type；重复删除返回 NOT_FOUND。 */
        if(!has_content_type){++missing_header_clears;return ESP_ERR_NOT_FOUND;}
        has_content_type=false;
    }else assert(has_content_type);
    return ESP_OK;}
esp_err_t esp_http_client_perform(void *p){
    (void)p;
    if(actual_method==HTTP_METHOD_HEAD){
        ++head_count;assert(!retained_body);
        if(head_outcome!=ESP_OK)return head_outcome;
    }else ++business_count;
    esp_http_client_event_t event={.user_data=config.user_data};
    event.event_id=HTTP_EVENT_ON_CONNECTED;config.event_handler(&event);
    if(sent){event.event_id=HTTP_EVENT_HEADERS_SENT;config.event_handler(&event);}
    if(location){event.event_id=HTTP_EVENT_ON_HEADER;event.header_key="location";event.header_value=location;config.event_handler(&event);}
    return actual_method==HTTP_METHOD_HEAD ? ESP_OK : outcome;
}
int esp_http_client_get_status_code(void *p){(void)p;return actual_method==HTTP_METHOD_HEAD ? 200 : status;}
esp_err_t esp_http_client_cleanup(void *p){(void)p;retained_body=NULL;has_content_type=false;++cleanup_count;return ESP_OK;}
esp_err_t esp_http_client_close(void *p){(void)p;++close_count;return ESP_OK;}
int main(void){
    char id[37]={0};int code;esp_err_t error;
    assert(sync_http_prepare()==ESP_OK && head_count==1 && business_count==0);
    assert(missing_header_clears>=2 && init_count==1 && cleanup_count==0);
    now+=24000000;assert(sync_http_prepare()==ESP_OK && head_count==1);
    now+=2000000;assert(sync_http_prepare()==ESP_OK && head_count==2 && init_count==1);
    now+=41000000;assert(sync_http_prepare()==ESP_OK && head_count==3 && close_count==1 && cleanup_count==0);
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_ACK);
    assert(code==201 && error==ESP_OK && !strcmp(id,"01234567-89ab-cdef-0123-456789abcdef"));
    assert(init_count==1 && !retained_body);
    method=HTTP_METHOD_PATCH;status=204;location=NULL;
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_ACK);
    assert(init_count==1 && cleanup_count==0);
    status=503;assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_RETRY);
    status=404;assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_BLOCK);
    outcome=ESP_ERR_TIMEOUT;status=0;
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_RETRY);
    assert(cleanup_count==0 && init_count==1 && close_count==2);
    id[0]=0;method=HTTP_METHOD_POST;
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_BLOCK);
    sent=false;assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_RETRY);
    /* 预探测失败时不发送 POST，也不把记录标成不明结果。 */
    head_outcome=ESP_ERR_TIMEOUT;const int before=business_count;
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_RETRY);
    assert(error==ESP_ERR_TIMEOUT && business_count==before && !id[0]);
    head_outcome=ESP_OK;outcome=ESP_OK;sent=true;status=201;
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_BLOCK);
    location="https://evil.example/api/telegraph/01234567-89ab-cdef-0123-456789abcdef";
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_BLOCK);
    location=NULL;status=429;
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_RETRY);
    /* 收到断连事件后，即使时间尚短，下次也先 HEAD 重连。 */
    esp_http_client_event_t disconnected={.event_id=HTTP_EVENT_DISCONNECTED,.user_data=config.user_data};
    config.event_handler(&disconnected);const int heads=head_count;
    assert(sync_http_prepare()==ESP_OK && head_count==heads+1 && init_count==1);
    /* 非空请求体的 NOT_FOUND 不能被吞掉，且失败发生于发送之前。 */
    json_error=ESP_ERR_NOT_FOUND;const int before_json=business_count;
    assert(sync_http_send(id,body,&code,&error)==SYNC_HTTP_RETRY);
    assert(error==ESP_ERR_NOT_FOUND && business_count==before_json);
    json_error=ESP_OK;
    sync_http_reset();assert(cleanup_count==1 && !retained_body);
    /* 清空请求体的其他错误仍报告，无法恢复时释放客户端。 */
    clear_error=ESP_ERR_NO_MEM;const int before_clear=head_count;
    assert(sync_http_prepare()==ESP_ERR_NO_MEM);
    assert(head_count==before_clear && init_count==2 && cleanup_count==2);
    clear_error=ESP_OK;
    puts("PASS: embedded private CA with hostname validation, missing Content-Type clear, strict other errors, startup/25s HEAD probes, stale close, probe failure before POST, POST/PATCH reuse, session-ticket configuration and retention, JSON lifetime, UUID and retry policy");
}
'''
with tempfile.TemporaryDirectory(prefix="esp32-http-tests-") as directory:
    work = Path(directory)
    (work / "test_sdk.h").write_text(SDK)
    for name in ("esp_err.h", "esp_http_client.h", "esp_crt_bundle.h", "sdkconfig.h", "esp_timer.h", "esp_log.h"):
        (work / name).write_text('#include "test_sdk.h"\n')
    (work / "mbedtls").mkdir()
    (work / "mbedtls/ssl.h").write_text('#include "test_sdk.h"\n')
    (work / "harness.c").write_text(HARNESS)
    component = ROOT / "components/telegraph_sync"
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
               "-I", str(work), "-I", str(component), "-I", str(ROOT / "components/morse/include"),
               str(work / "harness.c"), str(component / "sync_http.c"), str(component / "sync_outbox.c"),
               "-o", str(work / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "test")], check=True)
