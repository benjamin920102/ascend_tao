#include <cstdint>
#include "sha256d_core.h"
// Device-side preparation uses only 32-bit words (no device byte folding).
extern "C" void th_host_prepare_words(const uint8_t header[80],
                                       uint32_t prepared[32]) {
    uint32_t mid[8]; uint8_t tail[64];
    th_midstate(header, mid, tail);
    for (int i=0; i<32; ++i) prepared[i]=0;
    for (int i=0; i<8; ++i) prepared[i]=mid[i];
    for (int i=0; i<3; ++i) {
        int j=i*4;
        prepared[8+i]=((uint32_t)tail[j]<<24) | ((uint32_t)tail[j+1]<<16) |
                      ((uint32_t)tail[j+2]<<8) | (uint32_t)tail[j+3];
    }
}
extern "C" void th_host_prepare_target(const uint8_t target_le[32],
                                        uint32_t target_words[8]) {
    for (int i=0; i<8; ++i) {
        int j=i*4;
        target_words[i]=(uint32_t)target_le[j] |
                        ((uint32_t)target_le[j+1]<<8) |
                        ((uint32_t)target_le[j+2]<<16) |
                        ((uint32_t)target_le[j+3]<<24);
    }
}
extern "C" int th_host_verify(const uint8_t header[80],const uint8_t target[32],
                               uint32_t nonce,const uint8_t digest[32]) {
    uint8_t check[32];th_hash_header(header,nonce,check);
    for (int i=0;i<32;++i) if(check[i]!=digest[i]) return 0;
    return th_under_target(check,target);
}
