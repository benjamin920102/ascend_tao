#include <cstdint>
#include "sha256d_core.h"
extern "C" void th_host_prepare(const uint8_t header[80],uint8_t prepared[128]) {
    uint32_t mid[8]; uint8_t tail[64];
    th_midstate(header,mid,tail);
    for (int j=0;j<128;++j) prepared[j]=0;
    for (int i=0;i<8;++i) for (int j=0;j<4;++j)
        prepared[i*4+j]=(uint8_t)(mid[i]>>(8*j));
    for (int j=0;j<64;++j) prepared[32+j]=tail[j];
}
extern "C" int th_host_verify(const uint8_t header[80],const uint8_t target[32],
                               uint32_t nonce,const uint8_t digest[32]) {
    uint8_t check[32];th_hash_header(header,nonce,check);
    for (int i=0;i<32;++i) if(check[i]!=digest[i]) return 0;
    return th_under_target(check,target);
}
