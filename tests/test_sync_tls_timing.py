"""验证真实计时代码的分段累计、失败重试、原函数透传及首次连接过滤。"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;
typedef unsigned mbedtls_svc_key_id_t;
typedef unsigned psa_key_type_t;
typedef struct { size_t bits; psa_key_type_t type; } psa_key_attributes_t;
#define PSA_KEY_ATTRIBUTES_INIT {0}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_1 1
#define PSA_ALG_SHA_256 2
#define PSA_ALG_SHA_384 3
#define PSA_ALG_SHA_512 4
#define PSA_ALG_SIGN_GET_HASH(alg) ((alg)&0xff)
#define PSA_ALG_IS_RSA_PKCS1V15_SIGN(alg) (((alg)&0xff00)==0x100)
#define PSA_ALG_IS_RSA_PSS(alg) (((alg)&0xff00)==0x200)
#define PSA_ALG_IS_ECDSA(alg) (((alg)&0xff00)==0x300)
#define PSA_KEY_TYPE_IS_RSA(type) ((type)==1)
#define PSA_KEY_TYPE_IS_ECC(type) ((type)==2)
psa_status_t psa_get_key_attributes(mbedtls_svc_key_id_t,psa_key_attributes_t *);
void psa_reset_key_attributes(psa_key_attributes_t *);
static inline size_t psa_get_key_bits(const psa_key_attributes_t *a) {return a->bits;}
static inline psa_key_type_t psa_get_key_type(const psa_key_attributes_t *a) {return a->type;}
typedef int esp_err_t;
#define ESP_OK 0
const char *esp_err_to_name(int);
int64_t esp_timer_get_time(void);
void test_log(const char *, const char *, ...);
#define ESP_LOGI test_log
#define ESP_LOGW test_log
#define MBEDTLS_PRIVATE(name) name
typedef struct { int unused; } mbedtls_ssl_config;
typedef struct { int state; const mbedtls_ssl_config *conf; } mbedtls_ssl_context;
static inline const mbedtls_ssl_config *mbedtls_ssl_context_get_config(const mbedtls_ssl_context *ssl) {return ssl->conf;}
enum {
 MBEDTLS_SSL_HELLO_REQUEST, MBEDTLS_SSL_CLIENT_HELLO, MBEDTLS_SSL_SERVER_HELLO,
 MBEDTLS_SSL_SERVER_CERTIFICATE, MBEDTLS_SSL_SERVER_KEY_EXCHANGE,
 MBEDTLS_SSL_CERTIFICATE_REQUEST, MBEDTLS_SSL_SERVER_HELLO_DONE,
 MBEDTLS_SSL_CLIENT_CERTIFICATE, MBEDTLS_SSL_CLIENT_KEY_EXCHANGE,
 MBEDTLS_SSL_CERTIFICATE_VERIFY, MBEDTLS_SSL_CLIENT_CHANGE_CIPHER_SPEC,
 MBEDTLS_SSL_CLIENT_FINISHED, MBEDTLS_SSL_SERVER_CHANGE_CIPHER_SPEC,
 MBEDTLS_SSL_SERVER_FINISHED, MBEDTLS_SSL_FLUSH_BUFFERS,
 MBEDTLS_SSL_HANDSHAKE_WRAPUP, MBEDTLS_SSL_NEW_SESSION_TICKET,
 MBEDTLS_SSL_HANDSHAKE_OVER
};
'''
HARNESS = r'''
#include "sdk.h"
#include "sync_tls_timing.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static int64_t now=1000000, duration;
static int result, calls, signature_mode, attributes_calls, reset_calls, verify_calls;
static TaskHandle_t task=(void *)1;
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return task;}
psa_status_t __wrap_psa_verify_hash(mbedtls_svc_key_id_t,psa_algorithm_t,const uint8_t *,size_t,const uint8_t *,size_t);
static const uint8_t hash[]={1,2}, signature[]={3,4,5};
psa_status_t psa_get_key_attributes(mbedtls_svc_key_id_t key,psa_key_attributes_t *a) {
    ++attributes_calls;
    if(key==4)return -55;
    a->bits=key==1 ? 4096 : key==2 ? 2048 : 256;a->type=key==3 ? 2 : 1;
    return PSA_SUCCESS;
}
void psa_reset_key_attributes(psa_key_attributes_t *a) {++reset_calls;a->bits=0;a->type=0;}
psa_status_t __real_psa_verify_hash(mbedtls_svc_key_id_t key,psa_algorithm_t alg,const uint8_t *h,size_t hl,const uint8_t *s,size_t sl) {
    assert(alg && h==hash && hl==sizeof(hash) && s==signature && sl==sizeof(signature));
    ++verify_calls;now+=key==1 ? 10000000 : key==2 ? 500000 : 20000;
    return key==4 ? -88 : PSA_SUCCESS;
}
psa_status_t sync_verify_cached(mbedtls_svc_key_id_t key,psa_algorithm_t alg,const uint8_t *h,size_t hl,const uint8_t *s,size_t sl,bool *hit) {
    *hit=false;return __real_psa_verify_hash(key,alg,h,hl,s,sl);
}
static void verify(mbedtls_svc_key_id_t key,psa_algorithm_t alg) {
    assert(__wrap_psa_verify_hash(key,alg,hash,sizeof(hash),signature,sizeof(signature))==(key==4 ? -88 : PSA_SUCCESS));
}
static char logs[12000];
int __wrap_mbedtls_ssl_handshake_client_step(mbedtls_ssl_context *);
const char *esp_err_to_name(int error) {return error ? "FAILED" : "ESP_OK";}
int64_t esp_timer_get_time(void) {return now;}
void test_log(const char *tag,const char *format,...) {
    (void)tag;size_t len=strlen(logs);va_list ap;va_start(ap,format);
    vsnprintf(logs+len,sizeof(logs)-len,format,ap);va_end(ap);
    strcat(logs,"\n");
}
int __real_mbedtls_ssl_handshake_client_step(mbedtls_ssl_context *ssl) {
    ++calls;now+=duration;
    if(signature_mode) {
        verify(1,0x102);verify(2,0x203);verify(3,0x304);verify(4,0x405);
        if(signature_mode==2)for(int i=0;i<31;++i)verify(2,0x102);
    }
    ssl->state++;return result;
}
int main(void) {
    mbedtls_ssl_config conf={0}, foreign={0};
    mbedtls_ssl_context ssl={.state=MBEDTLS_SSL_SERVER_CERTIFICATE,.conf=&conf};
    /* 原函数参数与错误码透传；元数据查询失败仍执行真实验签。 */
    verify(1,0x102);assert(attributes_calls==0);
    sync_tls_timing_begin();sync_tls_timing_configured(&conf);
    signature_mode=1;result=-77;
    ssl.conf=&foreign;
    __wrap_mbedtls_ssl_handshake_client_step(&ssl);
    assert(attributes_calls==0); /* 其他 SSL 配置不记录。 */
    ssl.conf=&conf;ssl.state=MBEDTLS_SSL_SERVER_CERTIFICATE;
    __wrap_mbedtls_ssl_handshake_client_step(&ssl);
    /* 其他任务在握手外验签同样不记录。 */
    task=(void *)2;verify(2,0x102);task=(void *)1;
    sync_tls_timing_finish(-77);
    assert(strstr(logs,"TLS_VERIFY count=4 recorded=4 omitted=0"));
    assert(strstr(logs,"alg=RSA_PKCS1v1_5 hash=SHA256"));
    assert(strstr(logs,"key=RSA bits=4096 elapsed_us=10000000 result=0 attrs_result=0"));
    assert(strstr(logs,"alg=RSA_PSS hash=SHA384"));
    assert(strstr(logs,"key=RSA bits=2048 elapsed_us=500000"));
    assert(strstr(logs,"alg=ECDSA hash=SHA512"));
    assert(strstr(logs,"key=ECC bits=256 elapsed_us=20000"));
    assert(strstr(logs,"alg=OTHER hash=UNSPECIFIED"));
    assert(strstr(logs,"key=UNKNOWN bits=0 elapsed_us=20000 result=-88 attrs_result=-55"));
    assert(attributes_calls==4 && reset_calls==4 && verify_calls==10);
    /* 诊断缓存满不改变验签，明确报告超出数量。 */
    logs[0]=0;signature_mode=2;
    sync_tls_timing_begin();sync_tls_timing_configured(&conf);
    ssl.state=MBEDTLS_SSL_SERVER_CERTIFICATE;
    __wrap_mbedtls_ssl_handshake_client_step(&ssl);sync_tls_timing_finish(-77);
    assert(strstr(logs,"TLS_VERIFY count=35 recorded=32 omitted=3"));
    assert(verify_calls==45 && attributes_calls==36 && reset_calls==36);
    logs[0]=0;signature_mode=0;calls=0;duration=0;result=0;
    ssl.state=MBEDTLS_SSL_SERVER_CERTIFICATE;
    sync_tls_timing_begin();now+=25000;sync_tls_timing_configured(&conf);
    duration=13000;result=-77;
    assert(__wrap_mbedtls_ssl_handshake_client_step(&ssl)==-77 && calls==1);
    sync_tls_timing_finish(-77);
    assert(strstr(logs,"result=FAILED") && strstr(logs,"dns_tcp_setup=25 ms"));
    assert(strstr(logs,"stage=server_certificate_read_verify elapsed=13 ms calls=1"));
    logs[0]=0;
    /* 失败后重新采样；其他连接的状态不混入阶段数据。 */
    sync_tls_timing_begin();now+=40000;sync_tls_timing_configured(&conf);
    ssl.conf=&foreign;ssl.state=MBEDTLS_SSL_CLIENT_KEY_EXCHANGE;
    duration=999000;result=0;
    assert(__wrap_mbedtls_ssl_handshake_client_step(&ssl)==0 && calls==2);
    ssl.conf=&conf;ssl.state=MBEDTLS_SSL_SERVER_CERTIFICATE;duration=20000000;
    assert(__wrap_mbedtls_ssl_handshake_client_step(&ssl)==0);
    assert(ssl.state==MBEDTLS_SSL_SERVER_KEY_EXCHANGE);
    duration=2000000;__wrap_mbedtls_ssl_handshake_client_step(&ssl);
    /* WANT_READ 等重复步骤累计耗时和次数，不遗漏错误返回。 */
    ssl.state=MBEDTLS_SSL_CLIENT_KEY_EXCHANGE;duration=5000;result=-99;
    assert(__wrap_mbedtls_ssl_handshake_client_step(&ssl)==-99);
    ssl.state=MBEDTLS_SSL_CLIENT_KEY_EXCHANGE;duration=6000;result=0;
    __wrap_mbedtls_ssl_handshake_client_step(&ssl);
    sync_tls_timing_connected();now+=80000;sync_tls_timing_finish(ESP_OK);
    assert(strstr(logs,"stage=server_certificate_read_verify elapsed=20000 ms calls=1"));
    assert(strstr(logs,"stage=server_key_exchange_read_verify elapsed=2000 ms calls=1"));
    assert(strstr(logs,"stage=client_key_generation_agreement_write elapsed=11 ms calls=2"));
    assert(strstr(logs,"handshake=22011 ms after_connect=80 ms"));
    size_t logged=strlen(logs);
    /* 首次连接成功后，后续请求仍透传，不重复输出计时。 */
    sync_tls_timing_begin(); /* 复用时不再执行配置回调。 */
    __wrap_mbedtls_ssl_handshake_client_step(&ssl);
    sync_tls_timing_connected();sync_tls_timing_finish(ESP_OK);
    assert(strlen(logs)==logged && calls==7);
    puts("PASS: per-signature algorithm/key bits/timing, metadata failure, overflow, first TLS timing, failed-attempt reset, stage accumulation, unrelated-connection filtering and return/state passthrough");
}
'''
with tempfile.TemporaryDirectory(prefix="esp32-tls-timing-") as directory:
    work = Path(directory)
    (work / "sdk.h").write_text(SDK)
    (work / "mbedtls").mkdir()
    (work / "psa").mkdir()
    (work / "freertos").mkdir()
    for name in ("esp_err.h", "esp_log.h", "esp_timer.h", "mbedtls/ssl.h", "psa/crypto.h", "freertos/FreeRTOS.h", "freertos/task.h"):
        (work / name).write_text('#include "sdk.h"\n')
    (work / "harness.c").write_text(HARNESS)
    component = ROOT / "components/telegraph_sync"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(work), "-I", str(component), str(work / "harness.c"),
                    str(component / "sync_tls_timing.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
