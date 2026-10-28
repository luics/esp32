"""用 HTTP 和队列替身检查 DP 请求及分片响应，不访问线上服务。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sdk = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_SIZE, ESP_ERR_INVALID_ARG };
const char *esp_err_to_name(esp_err_t);
#define CONFIG_TELEGRAPH_SYNC_BASE_URL "https://example.test"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0
#define tskIDLE_PRIORITY 0
#define pdMS_TO_TICKS(x) (x)
QueueHandle_t xQueueCreate(int, size_t);
int xQueueSend(QueueHandle_t, const void *, int);
int xQueueReceive(QueueHandle_t, void *, int);
void vQueueDelete(QueueHandle_t);
int xTaskCreate(void (*)(void *), const char *, int, void *, int, void *);
void vTaskDelete(void *);
void vTaskDelay(int);
enum { HTTP_EVENT_ON_DATA, HTTP_METHOD_GET, HTTP_METHOD_POST };
typedef struct { int event_id; void *data; int data_len; void *user_data; } esp_http_client_event_t;
typedef struct { const char *url; int method, timeout_ms; const char *cert_pem; esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data; bool disable_auto_redirect; } esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
void *esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(void *, const char *, const char *);
esp_err_t esp_http_client_set_post_field(void *, const char *, int);
esp_err_t esp_http_client_perform(void *);
int esp_http_client_get_status_code(void *);
esp_err_t esp_http_client_cleanup(void *);
'''
harness = r'''
#include "sdk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sync_dp.c"
const char iot_ca_start[] __asm__("_binary_telegraph_iot_ca_crt_start") = "CA";
static dp_request_t items[4]; static int count, calls; static void (*worker)(void *); static esp_http_client_config_t config; static char body[256];
bool sync_dp_transport_ready(void) { return true; }
QueueHandle_t xQueueCreate(int n,size_t size){assert(n==4 && size==sizeof(dp_request_t));return items;}
int xQueueSend(QueueHandle_t q,const void *v,int wait){(void)q;assert(wait==0);if(count==4)return 0;items[count++]=*(const dp_request_t *)v;return 1;}
int xQueueReceive(QueueHandle_t q,void *v,int wait){(void)q;(void)wait;if(!count)return 0;*(dp_request_t *)v=items[0];memmove(items,items+1,(--count)*sizeof(*items));return 1;}
void vQueueDelete(QueueHandle_t q){(void)q;}
int xTaskCreate(void (*f)(void *),const char *n,int s,void *p,int priority,void *h){(void)n;(void)s;(void)p;(void)priority;(void)h;worker=f;return 1;}
void vTaskDelete(void *p){(void)p;} void vTaskDelay(int n){(void)n;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "error";}
void *esp_http_client_init(const esp_http_client_config_t *c){config=*c;assert(!strcmp(c->url,"https://example.test/api/dp") && c->cert_pem==iot_ca_start && c->disable_auto_redirect);body[0]=0;return &config;}
esp_err_t esp_http_client_set_header(void *p,const char *k,const char *v){(void)p;assert(!strcmp(k,"Content-Type")&&!strcmp(v,"application/json"));return ESP_OK;}
esp_err_t esp_http_client_set_post_field(void *p,const char *v,int n){(void)p;assert(n==(int)strlen(v));snprintf(body,sizeof(body),"%s",v);return ESP_OK;}
esp_err_t esp_http_client_perform(void *p){(void)p;calls++;if(calls==1){assert(config.method==HTTP_METHOD_POST && strstr(body,"true"));}else if(calls==2){assert(config.method==HTTP_METHOD_POST && strstr(body,"false"));}else assert(config.method==HTTP_METHOD_GET);const char *data=calls==1 ? "data: {\"choices\":[{\"delta\":{\"content\":\"Hi\"}}]}\r\n\r\ndata: {\"choices\":[{\"delta\":{\"content\":\" there\"}}]}\n\ndata: [DONE]\n\n" : "{\"choices\":[{\"message\":{\"content\":\"Hello\"}}]}"; for(size_t i=0;i<strlen(data);i++){esp_http_client_event_t e={HTTP_EVENT_ON_DATA,(void *)(data+i),1,config.user_data};config.event_handler(&e);}return ESP_OK;}
int esp_http_client_get_status_code(void *p){(void)p;return 200;} esp_err_t esp_http_client_cleanup(void *p){(void)p;return ESP_OK;}
static int starts;static char shown[80];
static void on_preview(bool start,const char *content,void *ctx){(void)ctx;if(start){if(starts==1)assert(!strcmp(shown,"Hi there"));starts++;shown[0]=0;}else strncat(shown,content,sizeof(shown)-strlen(shown)-1);}
int main(void){assert(telegraph_dp_post("{}") == ESP_ERR_INVALID_STATE);assert(sync_dp_start()==ESP_OK);telegraph_dp_set_content_callback(on_preview,NULL);assert(telegraph_dp_post(NULL)==ESP_ERR_INVALID_ARG);char json[]="{\"stream\":true}";assert(telegraph_dp_post(json)==ESP_OK);memset(json,'x',strlen(json));assert(telegraph_dp_post("{\"stream\":false}")==ESP_OK);assert(telegraph_dp_get_models()==ESP_OK);worker(NULL);assert(calls==3 && starts==2 && !strcmp(shown,"Hello"));return 0;}
'''
with tempfile.TemporaryDirectory(prefix="dp-test-") as temp:
    path = Path(temp)
    (path / "freertos").mkdir()
    (path / "sdk.h").write_text(sdk)
    for name in ["esp_err.h", "esp_http_client.h", "esp_log.h", "sdkconfig.h", "freertos/FreeRTOS.h", "freertos/queue.h", "freertos/task.h"]:
        (path / name).write_text('#include "sdk.h"\n')
    (path / "test.c").write_text(harness)
    binary = path / "test"
    cjson = root / "managed_components/espressif__cjson/cJSON"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-I", str(path), "-I", str(cjson), "-I", str(root / "components/telegraph_sync"), "-I", str(root / "components/telegraph_sync/include"), "-I", str(root / "components/morse/include"), str(path / "test.c"), str(root / "components/telegraph_sync/sync_dp_response.c"), str(cjson / "cJSON.c"), "-o", str(binary)], check=True)
    output = subprocess.check_output([str(binary)], text=True)
    assert 'data: [DONE]' in output and '"content":"Hello"' in output
print("PASS: DP GET, streaming and non-streaming POST, copied request body, fragmented response")
