/* Ascend CANN plugin ABI; host pointers only, no wallet/private keys. */
#ifndef TAOHASH_NPU_API_H
#define TAOHASH_NPU_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int th_create(int device, uint32_t grid, void **ctx);
/* attempts: 1..1048576; nonce starting at start, all u32. */
int th_scan(void *ctx, const uint8_t header80[80],const uint8_t targetLE[32],
            uint32_t start,uint32_t attempts,uint32_t *answer,int *found,uint8_t digest[32]);
void th_destroy(void *ctx);
#ifdef __cplusplus
}
#endif
#endif
