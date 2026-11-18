/* Bitcoin SHA-256 compression shared by native C++ and Ascend C device code. */
#ifndef TAOHASH_SHA_CORE_H
#define TAOHASH_SHA_CORE_H
#include <stdint.h>
#ifndef TAO_INLINE
#define TAO_INLINE static inline
#endif
TAO_INLINE uint32_t tao_rotr32(uint32_t a, uint32_t n) {
    return (a >> n) | (a << (32U - n));
}
TAO_INLINE uint64_t tao_rotl64(uint64_t a, uint32_t n) {
    return n ? ((a << n) | (a >> (64U - n))) : a;
}
TAO_INLINE void tao_sha_init(uint32_t h[8]) {
    const uint32_t v[8] = {
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U
    };
    for (int i = 0; i < 8; ++i) h[i] = v[i];
}
TAO_INLINE void tao_sha_words(uint32_t h[8], const uint32_t block_words[16]) {
    const uint32_t K[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,
        0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,
        0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,
        0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,
        0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,
        0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,
        0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,
        0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,
        0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    // 16-word circular message schedule: 48 fewer u32 words
    // per SHA compression compared with the original w[64].
    uint32_t w[16];
    for (int j = 0; j < 16; ++j) w[j] = block_words[j];

    uint32_t a=h[0],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7],bb=h[1];
    for (int j = 0; j < 64; ++j) {
        if (j >= 16) {
            const uint32_t p = w[(j + 1) & 15];
            const uint32_t q = w[(j + 14) & 15];
            const uint32_t s0 = tao_rotr32(p,7) ^ tao_rotr32(p,18) ^ (p >> 3);
            const uint32_t s1 = tao_rotr32(q,17) ^ tao_rotr32(q,19) ^ (q >> 10);
            w[j & 15] += s0 + w[(j + 9) & 15] + s1;
        }
        uint32_t T1 = v + (tao_rotr32(e,6) ^ tao_rotr32(e,11) ^ tao_rotr32(e,25))
                      + ((e & f) ^ ((~e) & g)) + K[j] + w[j & 15];
        uint32_t T2 = (tao_rotr32(a,2) ^ tao_rotr32(a,13) ^ tao_rotr32(a,22))
                      + ((a & bb) ^ (a & c) ^ (bb & c));
        v=g; g=f; f=e; e=d+T1; d=c; c=bb; bb=a; a=T1+T2;
    }
    h[0]+=a; h[1]+=bb; h[2]+=c; h[3]+=d;
    h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=v;
}

// Byte-oriented entry point for native code; the Ascend device path uses
// tao_sha_words directly to avoid compiler-generated wide endian shuffles.
TAO_INLINE void tao_sha_block(uint32_t h[8], const uint8_t b[64]) {
    uint32_t words[16];
    for (int j = 0; j < 16; ++j) {
        const int k = 4*j;
        words[j] = ((uint32_t)b[k] << 24) | ((uint32_t)b[k+1] << 16) |
                   ((uint32_t)b[k+2] << 8) | (uint32_t)b[k+3];
    }
    tao_sha_words(h, words);
}

#endif /* TAOHASH_SHA_CORE_H */
