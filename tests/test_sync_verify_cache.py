"""真实缓存实现的主机替身测试：输入绑定、失败回退、TTL、LRU，不绕过 X.509 流程。"""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;
typedef unsigned mbedtls_svc_key_id_t;
typedef struct {unsigned key;} psa_key_attributes_t;
#define PSA_KEY_ATTRIBUTES_INIT {0}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 0x02000009
#define PSA_EXPORT_PUBLIC_KEY_MAX_SIZE 1024
typedef struct {unsigned char bytes[4096];size_t length;} psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {0}
psa_status_t psa_get_key_attributes(unsigned,psa_key_attributes_t *);
void psa_reset_key_attributes(psa_key_attributes_t *);
unsigned psa_get_key_type(const psa_key_attributes_t *);
size_t psa_get_key_bits(const psa_key_attributes_t *);
unsigned psa_get_key_usage_flags(const psa_key_attributes_t *);
unsigned psa_get_key_algorithm(const psa_key_attributes_t *);
unsigned psa_get_key_enrollment_algorithm(const psa_key_attributes_t *);
unsigned psa_get_key_lifetime(const psa_key_attributes_t *);
psa_status_t psa_export_public_key(unsigned,uint8_t *,size_t,size_t *);
psa_status_t psa_hash_setup(psa_hash_operation_t *,psa_algorithm_t);
psa_status_t psa_hash_update(psa_hash_operation_t *,const uint8_t *,size_t);
psa_status_t psa_hash_finish(psa_hash_operation_t *,uint8_t *,size_t,size_t *);
psa_status_t psa_hash_abort(psa_hash_operation_t *);
int64_t esp_timer_get_time(void);
'''
HARNESS = r'''
#include "sdk.h"
#include "sync_verify_cache.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int64_t now=1000000;
static int calls, export_fail, hash_fail, attrs_fail, invalid;
static unsigned public_generation, policy;
static uint8_t hash[]={1,2}, sig[]={3,4};
int64_t esp_timer_get_time(void){return now;}
psa_status_t psa_get_key_attributes(unsigned key,psa_key_attributes_t *a){a->key=key;return attrs_fail ? -1 : 0;}
void psa_reset_key_attributes(psa_key_attributes_t *a){a->key=0;}
unsigned psa_get_key_type(const psa_key_attributes_t *a){(void)a;return 1;}
size_t psa_get_key_bits(const psa_key_attributes_t *a){(void)a;return 4096;}
unsigned psa_get_key_usage_flags(const psa_key_attributes_t *a){(void)a;return 8192+policy;}
unsigned psa_get_key_algorithm(const psa_key_attributes_t *a){(void)a;return 0x102;}
unsigned psa_get_key_enrollment_algorithm(const psa_key_attributes_t *a){(void)a;return 0;}
unsigned psa_get_key_lifetime(const psa_key_attributes_t *a){(void)a;return 0;}
psa_status_t psa_export_public_key(unsigned key,uint8_t *out,size_t cap,size_t *len){
 assert(cap>=8);memcpy(out,&key,4);memcpy(out+4,&public_generation,4);*len=8;return export_fail ? -1 : 0;
}
psa_status_t psa_hash_setup(psa_hash_operation_t *o,psa_algorithm_t a){assert(a==PSA_ALG_SHA_256);o->length=0;return hash_fail ? -1 : 0;}
psa_status_t psa_hash_update(psa_hash_operation_t *o,const uint8_t *b,size_t n){assert(o->length+n<4096);memcpy(o->bytes+o->length,b,n);o->length+=n;return 0;}
/* 测试替身以完整输入匹配分配唯一摘要，验证输入绑定；固件使用真实 PSA SHA-256。 */
static psa_hash_operation_t preimages[300];static unsigned count;
psa_status_t psa_hash_finish(psa_hash_operation_t *o,uint8_t *out,size_t cap,size_t *len){
 assert(cap>=32);unsigned i=0;for(;i<count;++i)if(o->length==preimages[i].length&&!memcmp(o->bytes,preimages[i].bytes,o->length))break;
 if(i==count){assert(count<300);preimages[count++]=*o;}memset(out,0,32);memcpy(out,&i,4);*len=32;return 0;
}
psa_status_t psa_hash_abort(psa_hash_operation_t *o){o->length=0;return 0;}
psa_status_t __real_psa_verify_hash(unsigned key,unsigned alg,const uint8_t *h,size_t hl,const uint8_t *s,size_t sl){
 assert(key&&alg&&h==hash&&hl==2&&s==sig&&sl==2);++calls;return invalid ? -88 : 0;
}
static bool check(unsigned key,unsigned alg){bool hit=true;assert(sync_verify_cached(key,alg,hash,2,sig,2,&hit)==(invalid ? -88 : 0));return hit;}
int main(void){
 assert(!check(1,0x102)&&calls==1);assert(check(1,0x102)&&calls==1);
 assert(!check(2,0x102));assert(!check(1,0x202));
 ++hash[0];assert(!check(1,0x102));--hash[0];assert(check(1,0x102));
 ++sig[0];assert(!check(1,0x102));--sig[0];assert(check(1,0x102));
 ++public_generation;assert(!check(1,0x102));--public_generation;assert(check(1,0x102));
 ++policy;assert(!check(1,0x102));--policy;assert(check(1,0x102));
 invalid=1;assert(!check(3,0x102));assert(!check(3,0x102));invalid=0;assert(!check(3,0x102));assert(check(3,0x102));
 export_fail=1;assert(!check(1,0x102));export_fail=0;
 hash_fail=1;assert(!check(1,0x102));hash_fail=0;
 attrs_fail=1;assert(!check(1,0x102));attrs_fail=0;
 now+=3600LL*1000000;assert(!check(1,0x102));assert(check(1,0x102));
 sync_verify_cache_clear();
 for(unsigned key=1;key<=32;++key){++now;assert(!check(key,0x102));}
 ++now;assert(check(1,0x102));++now;assert(!check(33,0x102));assert(check(1,0x102));assert(!check(2,0x102));
 sync_verify_cache_clear();assert(!check(1,0x102));
 puts("PASS: signature cache binds public key/policy/algorithm/hash/signature, success-only, safe fallback, one-hour TTL and bounded LRU");
}
'''
with tempfile.TemporaryDirectory(prefix="esp32-verify-cache-") as directory:
    work=Path(directory)
    (work/'psa').mkdir()
    (work/'sdk.h').write_text(SDK)
    for name in ('psa/crypto.h','esp_timer.h'):
        (work/name).write_text('#include "sdk.h"\n')
    (work/'harness.c').write_text(HARNESS)
    component=ROOT/'components/telegraph_sync'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(work),'-I',str(component),str(work/'harness.c'),str(component/'sync_verify_cache.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
