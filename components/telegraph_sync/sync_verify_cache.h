#pragma once
#include <stdbool.h>
#include "psa/crypto.h"

/* 仅同步任务调用。缓存成功的数学验签，X.509 信任链、时间和主机名检查仍正常执行。 */
psa_status_t sync_verify_cached(mbedtls_svc_key_id_t key, psa_algorithm_t algorithm,
                                const uint8_t *hash, size_t hash_length,
                                const uint8_t *signature, size_t signature_length, bool *hit);
void sync_verify_cache_clear(void);
