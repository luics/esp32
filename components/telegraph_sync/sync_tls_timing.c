#include "sync_tls_timing.h"
#include "sync_verify_cache.h"
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/ssl.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ESP-IDF 6.1 的 TLS 1.2 客户端跨编译单元调用此函数。
 * 链接包装只观测原状态机，不改 SDK、返回值或握手流程；升级 IDF 时需复核该入口与 state 字段。 */
int __real_mbedtls_ssl_handshake_client_step(mbedtls_ssl_context *ssl);

typedef struct { int64_t elapsed; unsigned calls; } stage_t;
/* 上限仅约束诊断缓存，超过时仍照常验签，并明确报告未记录的数量。 */
#define VERIFY_SAMPLES 32
typedef struct {
    psa_algorithm_t algorithm;
    psa_key_type_t key_type;
    size_t bits;
    int stage;
    int64_t elapsed;
    psa_status_t result, attributes_result;
    bool cache_hit;
} verify_t;
psa_status_t __real_psa_verify_hash(mbedtls_svc_key_id_t key, psa_algorithm_t algorithm,
                                   const uint8_t *hash, size_t hash_length,
                                   const uint8_t *signature, size_t signature_length);
static struct {
    bool active;
    const void *config;
    int64_t started, configured, handshake, connected;
    stage_t stages[MBEDTLS_SSL_HANDSHAKE_OVER];
    TaskHandle_t task;
    bool in_step;
    int current_stage;
    unsigned verify_count;
    verify_t verifies[VERIFY_SAMPLES];
} timing;

static const char *stage_name(int state)
{
    switch (state) {
    case MBEDTLS_SSL_HELLO_REQUEST: return "initialize";
    case MBEDTLS_SSL_CLIENT_HELLO: return "client_hello";
    case MBEDTLS_SSL_SERVER_HELLO: return "server_hello_wait_parse";
    case MBEDTLS_SSL_SERVER_CERTIFICATE: return "server_certificate_read_verify";
    case MBEDTLS_SSL_SERVER_KEY_EXCHANGE: return "server_key_exchange_read_verify";
    case MBEDTLS_SSL_CERTIFICATE_REQUEST: return "certificate_request";
    case MBEDTLS_SSL_SERVER_HELLO_DONE: return "server_hello_done";
    case MBEDTLS_SSL_CLIENT_CERTIFICATE: return "client_certificate";
    case MBEDTLS_SSL_CLIENT_KEY_EXCHANGE: return "client_key_generation_agreement_write";
    case MBEDTLS_SSL_CERTIFICATE_VERIFY: return "client_certificate_verify";
    case MBEDTLS_SSL_CLIENT_CHANGE_CIPHER_SPEC: return "client_cipher_spec_derive_keys";
    case MBEDTLS_SSL_CLIENT_FINISHED: return "client_finished";
    case MBEDTLS_SSL_SERVER_CHANGE_CIPHER_SPEC: return "server_ticket_or_cipher_spec_wait";
    case MBEDTLS_SSL_NEW_SESSION_TICKET: return "server_session_ticket_wait";
    case MBEDTLS_SSL_SERVER_FINISHED: return "server_finished_wait_verify";
    case MBEDTLS_SSL_FLUSH_BUFFERS: return "flush_buffers";
    case MBEDTLS_SSL_HANDSHAKE_WRAPUP: return "handshake_wrapup";
    default: return "other";
    }
}

void sync_tls_timing_begin(void)
{
    memset(&timing, 0, sizeof(timing));
    timing.active = true;
    timing.task = xTaskGetCurrentTaskHandle();
    timing.started = esp_timer_get_time();
}

void sync_tls_timing_configured(const void *config)
{
    if (!timing.active) return;
    timing.config = config;
    timing.configured = esp_timer_get_time();
}

int __wrap_mbedtls_ssl_handshake_client_step(mbedtls_ssl_context *ssl)
{
    /* 按 SSL 配置过滤，其他网络任务不计入当前请求。状态读取局限在本诊断模块。 */
    if (!timing.active || xTaskGetCurrentTaskHandle() != timing.task ||
        mbedtls_ssl_context_get_config(ssl) != timing.config)
        return __real_mbedtls_ssl_handshake_client_step(ssl);
    const int state = ssl->MBEDTLS_PRIVATE(state);
    const int64_t started = esp_timer_get_time();
    if (!timing.handshake) timing.handshake = started;
    timing.current_stage = state;
    timing.in_step = true;
    const int result = __real_mbedtls_ssl_handshake_client_step(ssl);
    timing.in_step = false;
    if (state >= 0 && state < MBEDTLS_SSL_HANDSHAKE_OVER) {
        timing.stages[state].elapsed += esp_timer_get_time() - started;
        ++timing.stages[state].calls;
    }
    return result;
}

static const char *signature_name(psa_algorithm_t algorithm)
{
    if (PSA_ALG_IS_RSA_PKCS1V15_SIGN(algorithm)) return "RSA_PKCS1v1_5";
    if (PSA_ALG_IS_RSA_PSS(algorithm)) return "RSA_PSS";
    if (PSA_ALG_IS_ECDSA(algorithm)) return "ECDSA";
    return "OTHER";
}

static const char *hash_name(psa_algorithm_t algorithm)
{
    switch (PSA_ALG_SIGN_GET_HASH(algorithm)) {
    case PSA_ALG_SHA_1: return "SHA1";
    case PSA_ALG_SHA_256: return "SHA256";
    case PSA_ALG_SHA_384: return "SHA384";
    case PSA_ALG_SHA_512: return "SHA512";
    default: return "UNSPECIFIED";
    }
}

psa_status_t __wrap_psa_verify_hash(mbedtls_svc_key_id_t key, psa_algorithm_t algorithm,
                                   const uint8_t *hash, size_t hash_length,
                                   const uint8_t *signature, size_t signature_length)
{
    /* PSA 同时服务其他任务，只采集本次客户端握手步骤内的验签。 */
    if (!timing.active || xTaskGetCurrentTaskHandle() != timing.task || !timing.in_step)
        return __real_psa_verify_hash(key, algorithm, hash, hash_length, signature, signature_length);
    const unsigned index = timing.verify_count++;
    verify_t *sample = index < VERIFY_SAMPLES ? &timing.verifies[index] : NULL;
    if (sample) {
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        sample->attributes_result = psa_get_key_attributes(key, &attributes);
        if (sample->attributes_result == PSA_SUCCESS) {
            sample->key_type = psa_get_key_type(&attributes);
            sample->bits = psa_get_key_bits(&attributes);
        }
        psa_reset_key_attributes(&attributes);
        sample->algorithm = algorithm;
        sample->stage = timing.current_stage;
    }
    /* 单独计量实际验签，查询公钥元数据和串口输出不计入此耗时。 */
    const int64_t started = esp_timer_get_time();
    bool cache_hit;
    const psa_status_t result = sync_verify_cached(key, algorithm, hash, hash_length,
                                                   signature, signature_length, &cache_hit);
    const int64_t elapsed = esp_timer_get_time() - started;
    if (sample) {
        sample->elapsed = elapsed;
        sample->result = result;
        sample->cache_hit = cache_hit;
    }
    return result;
}

void sync_tls_timing_connected(void)
{
    if (timing.active && !timing.connected) timing.connected = esp_timer_get_time();
}

void sync_tls_timing_finish(esp_err_t error)
{
    if (!timing.active) return;
    const int64_t ended = esp_timer_get_time();
    timing.active = false;
    if (!timing.configured && !timing.handshake) return; /* 复用请求不重复输出握手日志。 */
    /* 握手期间只采样，结束后集中打印，避免串口输出污染阶段计时或泄露 TLS 内容。 */
    ESP_LOGI("telegraph_sync", "TLS_TIMING result=%s request_total=%" PRId64 " ms", esp_err_to_name(error),
             (ended - timing.started) / 1000);
    if (timing.configured) {
        ESP_LOGI("telegraph_sync", "TLS_TIMING dns_tcp_setup=%" PRId64 " ms",
                 (timing.configured - timing.started) / 1000);
    }
    if (timing.handshake && timing.connected) {
        ESP_LOGI("telegraph_sync", "TLS_TIMING handshake=%" PRId64 " ms after_connect=%" PRId64 " ms",
                 (timing.connected - timing.handshake) / 1000, (ended - timing.connected) / 1000);
    }
    for (int state = 0; state < MBEDTLS_SSL_HANDSHAKE_OVER; ++state) {
        const stage_t *stage = &timing.stages[state];
        if (stage->calls) {
            ESP_LOGI("telegraph_sync", "TLS_TIMING stage=%s elapsed=%" PRId64 " ms calls=%u",
                     stage_name(state), stage->elapsed / 1000, stage->calls);
        }
    }
    const unsigned recorded = timing.verify_count < VERIFY_SAMPLES ? timing.verify_count : VERIFY_SAMPLES;
    ESP_LOGI("telegraph_sync", "TLS_VERIFY count=%u recorded=%u omitted=%u", timing.verify_count,
             recorded, timing.verify_count - recorded);
    for (unsigned i = 0; i < recorded; ++i) {
        const verify_t *sample = &timing.verifies[i];
        const char *key_type = PSA_KEY_TYPE_IS_RSA(sample->key_type) ? "RSA" :
                               PSA_KEY_TYPE_IS_ECC(sample->key_type) ? "ECC" : "UNKNOWN";
        ESP_LOGI("telegraph_sync", "TLS_VERIFY index=%u stage=%s alg=%s hash=%s alg_id=0x%08" PRIx32
                 " key=%s bits=%zu elapsed_us=%" PRId64 " result=%" PRId32 " attrs_result=%" PRId32 " cache_hit=%u",
                 i + 1, stage_name(sample->stage), signature_name(sample->algorithm),
                 hash_name(sample->algorithm), (uint32_t)sample->algorithm, key_type, sample->bits,
                 sample->elapsed, (int32_t)sample->result, (int32_t)sample->attributes_result, sample->cache_hit ? 1U : 0U);
    }
    if (timing.configured && !timing.handshake) {
        ESP_LOGW("telegraph_sync", "TLS_TIMING no TLS 1.2 steps observed; check linker hook or negotiated TLS version");
    }
}
