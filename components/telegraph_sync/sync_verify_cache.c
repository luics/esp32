#include "sync_verify_cache.h"
#include <string.h>
#include "esp_timer.h"

#define CACHE_ENTRIES 32
#define CACHE_TTL_US (3600LL * 1000000)
typedef struct {
    bool valid;
    uint8_t digest[32];
    int64_t saved, used;
} entry_t;
static entry_t entries[CACHE_ENTRIES];

psa_status_t __real_psa_verify_hash(mbedtls_svc_key_id_t key, psa_algorithm_t algorithm,
                                   const uint8_t *hash, size_t hash_length,
                                   const uint8_t *signature, size_t signature_length);

void sync_verify_cache_clear(void)
{
    memset(entries, 0, sizeof(entries));
}

static psa_status_t hash_number(psa_hash_operation_t *operation, uint64_t number)
{
    uint8_t encoded[8];
    for (unsigned i = 0; i < sizeof(encoded); ++i) encoded[7 - i] = (uint8_t)(number >> (8 * i));
    return psa_hash_update(operation, encoded, sizeof(encoded));
}

static bool fingerprint(mbedtls_svc_key_id_t key, psa_algorithm_t algorithm,
                        const uint8_t *hash, size_t hash_length,
                        const uint8_t *signature, size_t signature_length, uint8_t digest[32])
{
    /* 不使用可回收的 key id 作为缓存键，绑定公钥内容及 PSA 策略，避免换钥或策略变化后误命中。 */
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    uint8_t public_key[PSA_EXPORT_PUBLIC_KEY_MAX_SIZE];
    size_t public_length = 0, digest_length = 0;
    bool ok = false;
    if (psa_get_key_attributes(key, &attributes) != PSA_SUCCESS) goto cleanup;
    if (psa_export_public_key(key, public_key, sizeof(public_key), &public_length) != PSA_SUCCESS) goto cleanup;
    if (psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS) goto cleanup;
    const uint64_t fields[] = {
        algorithm, psa_get_key_type(&attributes), psa_get_key_bits(&attributes),
        psa_get_key_usage_flags(&attributes), psa_get_key_algorithm(&attributes),
        psa_get_key_enrollment_algorithm(&attributes), psa_get_key_lifetime(&attributes),
        public_length, hash_length, signature_length
    };
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (hash_number(&operation, fields[i]) != PSA_SUCCESS) goto cleanup;
    }
    if (psa_hash_update(&operation, public_key, public_length) != PSA_SUCCESS ||
        psa_hash_update(&operation, hash, hash_length) != PSA_SUCCESS ||
        psa_hash_update(&operation, signature, signature_length) != PSA_SUCCESS) goto cleanup;
    ok = psa_hash_finish(&operation, digest, 32, &digest_length) == PSA_SUCCESS && digest_length == 32;
cleanup:
    /* 公钥查询、导出或摘要失败一律回退真实验签，不因缓存故障放行。 */
    psa_hash_abort(&operation);
    psa_reset_key_attributes(&attributes);
    return ok;
}

psa_status_t sync_verify_cached(mbedtls_svc_key_id_t key, psa_algorithm_t algorithm,
                                const uint8_t *hash, size_t hash_length,
                                const uint8_t *signature, size_t signature_length, bool *hit)
{
    *hit = false;
    uint8_t digest[32];
    const bool eligible = fingerprint(key, algorithm, hash, hash_length, signature, signature_length, digest);
    const int64_t now = esp_timer_get_time();
    entry_t *slot = &entries[0];
    if (eligible) {
        for (unsigned i = 0; i < CACHE_ENTRIES; ++i) {
            entry_t *entry = &entries[i];
            if (entry->valid && (now < entry->saved || now - entry->saved >= CACHE_TTL_US)) entry->valid = false;
            if (entry->valid && !memcmp(entry->digest, digest, sizeof(digest))) {
                entry->used = now;
                *hit = true;
                return PSA_SUCCESS;
            }
            if (!entry->valid || (slot->valid && entry->used < slot->used)) slot = entry;
        }
    }
    const psa_status_t result = __real_psa_verify_hash(key, algorithm, hash, hash_length,
                                                      signature, signature_length);
    /* 只缓存数学验签成功；时间、主机名、信任路径等仍由每次 X.509 验证独立判断。 */
    if (eligible && result == PSA_SUCCESS) {
        memcpy(slot->digest, digest, sizeof(digest));
        slot->saved = slot->used = esp_timer_get_time();
        slot->valid = true;
    }
    return result;
}
