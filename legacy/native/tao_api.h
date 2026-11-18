/*
 * Native C ABI v3 for the Ascend TAO registration-PoW scanner.
 * All pointers except the opaque context refer to HOST memory.
 * Return: 0 success, -1 invalid argument, -2 invalid NPU candidate,
 *         positive ACL/runtime error code.
 * No wallet keys or networking are involved in this API.
 */
#ifndef ASCEND_TAO_NATIVE_API_H
#define ASCEND_TAO_NATIVE_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int tao_api_version(void);
int tao_create(int device, uint32_t grid, void **context);
int tao_mine(void *context, const uint8_t prefix[123], const uint8_t target_le[32],
             uint64_t start, uint32_t attempts, uint64_t *answer,
             int *found, uint8_t seal[32]);
int tao_mine_window(void *context, const uint8_t prefix[123], const uint8_t target_le[32],
                    uint64_t start, uint64_t attempts, uint32_t kernel_chunk,
                    uint32_t max_ms, uint64_t *answer, int *found,
                    uint8_t seal[32], uint64_t *scanned);
void tao_destroy(void *context);
#ifdef __cplusplus
}
#endif
#endif
