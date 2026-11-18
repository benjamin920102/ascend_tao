/*
 * SPDX-License-Identifier: MIT
 * Pure arithmetic TAO PoW primitive shared by native C++ host verification
 * and the Ascend Vector kernel. NO Pearl / BLAKE3 code is used.
 * Work = Keccak-256(SHA-256(prefix123 || LE64(nonce))).
 * NOTE: This is Keccak (domain 0x01), NOT standardized SHA3-256 (0x06).
 */
#ifndef TAO_HASH_CORE_H
#define TAO_HASH_CORE_H
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
TAO_INLINE void tao_sha_block(uint32_t h[8], const uint8_t b[64]) {
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
    for (int j = 0; j < 16; ++j) {
        int i = 4*j;
        w[j] = ((uint32_t)b[i] << 24) | ((uint32_t)b[i+1] << 16) |
               ((uint32_t)b[i+2] << 8) | (uint32_t)b[i+3];
    }

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
TAO_INLINE void tao_keccak256_sha_state(const uint32_t digest[8], uint8_t seal[32]) {
    const uint64_t rc[24] = {
        0x0000000000000001ULL,0x0000000000008082ULL,0x800000000000808aULL,
        0x8000000080008000ULL,0x000000000000808bULL,0x0000000080000001ULL,
        0x8000000080008081ULL,0x8000000000008009ULL,0x000000000000008aULL,
        0x0000000000000088ULL,0x0000000080008009ULL,0x000000008000000aULL,
        0x000000008000808bULL,0x800000000000008bULL,0x8000000000008089ULL,
        0x8000000000008003ULL,0x8000000000008002ULL,0x8000000000000080ULL,
        0x000000000000800aULL,0x800000008000000aULL,0x8000000080008081ULL,
        0x8000000000008080ULL,0x0000000080000001ULL,0x8000000080008008ULL
    };
    const uint8_t rho[25] = {0,1,62,28,27,36,44,6,55,20,3,10,43,25,39,41,45,15,21,8,18,2,61,56,14};
    uint64_t a[25]={0};
    for (int i=0; i<32; ++i) {
        uint8_t d=(uint8_t)(digest[i/4] >> (24-8*(i%4)));
        a[i/8] ^= (uint64_t)d << (8*(i%8));
    }
    // 32-byte message at start of the 136-byte Keccak-256 rate block.
    a[4] ^= 0x01ULL;                   // Keccak padding, byte 32
    a[16] ^= 0x8000000000000000ULL;   // final rate byte (index 135)
    for (int round=0; round<24; ++round) {
        uint64_t c[5],d[5],b[25];
        for (int x=0;x<5;++x) c[x]=a[x]^a[x+5]^a[x+10]^a[x+15]^a[x+20];
        for (int x=0;x<5;++x) d[x]=c[(x+4)%5]^tao_rotl64(c[(x+1)%5],1);
        for (int y=0;y<5;++y) for (int x=0;x<5;++x) {
            int i=x+5*y;
            b[y+5*((2*x+3*y)%5)] = tao_rotl64(a[i]^d[x],rho[i]);
        }
        for (int y=0;y<5;++y) for (int x=0;x<5;++x)
            a[x+5*y] = b[x+5*y] ^ ((~b[((x+1)%5)+5*y]) & b[((x+2)%5)+5*y]);
        a[0] ^= rc[round];
    }
    for (int i=0;i<32;++i) seal[i]=(uint8_t)(a[i/8]>>(8*(i%8)));
}
TAO_INLINE void tao_prepare(const uint8_t prefix[123], uint32_t midstate[8], uint8_t tail[128]) {
    tao_sha_init(midstate);
    tao_sha_block(midstate, prefix);
    for (int i=0;i<128;++i) tail[i]=0;
    for (int i=0;i<59;++i) tail[i]=prefix[64+i];
    tail[67]=0x80;
    // 131 bytes x 8 bits = 1048 bits (0x0418), SHA-256 big-endian length.
    tail[126]=0x04; tail[127]=0x18;
}
TAO_INLINE void tao_make_seal(const uint32_t midstate[8], uint8_t tail[128],
                              uint64_t nonce, uint8_t seal[32]) {
    for (int i=0;i<8;++i) tail[59+i]=(uint8_t)(nonce>>(8*i));
    uint32_t h[8];
    for (int i=0;i<8;++i) h[i]=midstate[i];
    tao_sha_block(h,tail);
    tao_sha_block(h,tail+64);
    tao_keccak256_sha_state(h,seal);
}
TAO_INLINE void tao_seal(const uint8_t prefix[123], uint64_t nonce, uint8_t seal[32]) {
    uint32_t midstate[8]; uint8_t tail[128];
    tao_prepare(prefix,midstate,tail);
    tao_make_seal(midstate,tail,nonce,seal);
}
TAO_INLINE int tao_meets_target(const uint8_t seal[32], const uint8_t target[32]) {
    // Runtime interprets seal as U256 little endian; compare MSB first.
    for (int i=31;i>=0;--i) {
        if (seal[i]<target[i]) return 1;
        if (seal[i]>target[i]) return 0;
    }
    return 1;
}
#endif /* TAO_HASH_CORE_H */
