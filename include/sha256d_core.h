/*
 * SPDX-License-Identifier: MIT
 * Bitcoin SHA256d of an 80-byte block header, optimized for fixed 76-byte
 * prefix and varying 4-byte little-endian nonce. No Keccak or registration PoW.
 * Digest is the 32 raw SHA256 bytes; compare as little-endian 256-bit integer.
 */
#ifndef TAOHASH_SHA256D_CORE_H
#define TAOHASH_SHA256D_CORE_H
#include <stdint.h>
#include "sha_core.h"
TAO_INLINE void th_midstate(const uint8_t header[80], uint32_t state[8], uint8_t tail[64]) {
    tao_sha_init(state);
    tao_sha_block(state, header);
    for (int j=0;j<64;++j) tail[j]=0;
    for (int j=0;j<12;++j) tail[j]=header[64+j];
    tail[16]=0x80; /* second SHA block: final 16 B + padding */
    tail[62]=0x02; tail[63]=0x80; /* 80*8 = 640 bits, big endian */
}
TAO_INLINE void th_hash_midstate(const uint32_t first[8], uint8_t tail[64],
                                  uint32_t nonce, uint8_t digest[32]) {
    for (int i=0;i<4;++i) tail[12+i]=(uint8_t)(nonce>>(i*8));
    uint32_t a[8];
    for (int i=0;i<8;++i) a[i]=first[i];
    tao_sha_block(a,tail);
    uint8_t b[64]={0};
    for (int i=0;i<8;++i) for (int j=0;j<4;++j)
        b[4*i+j]=(uint8_t)(a[i]>>(24-j*8));
    b[32]=0x80;
    b[62]=0x01; /* second hash length: 32*8 = 256 bits */
    uint32_t c[8]; tao_sha_init(c); tao_sha_block(c,b);
    for (int i=0;i<8;++i) for (int j=0;j<4;++j)
        digest[4*i+j]=(uint8_t)(c[i]>>(24-j*8));
}
TAO_INLINE void th_hash_header(const uint8_t header[80],uint32_t nonce,uint8_t digest[32]) {
    uint32_t mid[8];uint8_t tail[64];
    th_midstate(header,mid,tail);
    th_hash_midstate(mid,tail,nonce,digest);
}
TAO_INLINE int th_under_target(const uint8_t digest[32],const uint8_t targetLE[32]) {
    for (int i=31;i>=0;--i) {
        if (digest[i]<targetLE[i]) return 1;
        if (digest[i]>targetLE[i]) return 0;
    }
    return 1;
}
#endif
