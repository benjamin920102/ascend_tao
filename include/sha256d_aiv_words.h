/*
 * ASC/DAV-2201 SHA256d nonce scanner: 32-bit-word-only hot path.
 * No digest byte-array packing, implicit 64-bit bswap, Cube or cross-core GM.
 * Input words are host-prepared; the 4-byte nonce is SHA big-endian in block 2.
 */
#ifndef TAOHASH_SHA256D_AIV_WORDS_H
#define TAOHASH_SHA256D_AIV_WORDS_H
#include <stdint.h>
#include "sha_core.h"

// Keep this conversion as four explicit *volatile byte loads* to prevent the
// Bisheng 15.0.5 optimizer from folding the endian shuffle into an unsupported
// bswap.i64 DAG node on DAV-2201. The host is aarch64 little-endian.
// Benchmark on the NPU before replacing with a faster intrinsic.
TAO_INLINE uint32_t th_aiv_nonce_be(uint32_t nonce) {
    volatile uint32_t le = nonce;
    const volatile uint8_t *b = reinterpret_cast<const volatile uint8_t *>(&le);
    const uint32_t b0 = b[0];
    const uint32_t b1 = b[1];
    const uint32_t b2 = b[2];
    const uint32_t b3 = b[3];
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

// Target is the host-prepared 8 x LE32 numeric words. SHA compression emits
// 8 x BE-serialized words; compare byte 31 first, down to byte 0.
// Avoid a reverse/bswap of every digest word for each nonce.
TAO_INLINE int th_aiv_under_target(const uint32_t digest[8],
                                   const uint32_t target_le[8]) {
    for (int i = 7; i >= 0; --i) {
        uint32_t d = digest[i];
        uint32_t t = target_le[i];
        // d low byte is digest byte[4*i+3]; t high byte is target byte[4*i+3]
        for (int j = 0; j < 4; ++j) {
            uint32_t dv = (d >> (j*8)) & 255U;
            uint32_t tv = (t >> (24-j*8)) & 255U;
            if (dv < tv) return 1;
            if (dv > tv) return 0;
        }
    }
    return 1;
}

TAO_INLINE void th_aiv_hash(const uint32_t midstate[8],
                            const uint32_t tail_prefix[3],
                            uint32_t nonce, uint32_t digest[8]) {
    uint32_t block2[16] = {0};
    block2[0] = tail_prefix[0];
    block2[1] = tail_prefix[1];
    block2[2] = tail_prefix[2];
    block2[3] = th_aiv_nonce_be(nonce);
    block2[4] = 0x80000000U;
    block2[15] = 640U;       // Bitcoin 80-byte header in bits

    uint32_t state[8];
    for (int i = 0; i < 8; ++i) state[i] = midstate[i];
    tao_sha_words(state, block2);

    uint32_t last[16] = {0};
    for (int i = 0; i < 8; ++i) last[i] = state[i];
    last[8] = 0x80000000U;
    last[15] = 256U;         // SHA256 of 32 digest bytes
    tao_sha_init(digest);
    tao_sha_words(digest, last);
}
#endif
