/* Native host-only SHA256 preparation and hit verification.
 * Split from .asc deliberately: __aicore__ inline is device-only. */
#include <cstdint>
#include <cstring>
#include "tao_hash_core.h"
extern "C" void tao_prepare_host(const uint8_t* prefix,uint8_t* prepared){
    uint32_t midstate[8];uint8_t tail[128];
    tao_prepare(prefix,midstate,tail);
    std::memset(prepared,0,128);
    for(int i=0;i<8;++i){
        const uint32_t x=midstate[i];
        for(int j=0;j<4;++j)prepared[4*i+j]=static_cast<uint8_t>(x>>(8*j));
    }
    std::memcpy(prepared+32,prefix+64,59);
}
extern "C" int tao_verify_host(const uint8_t* prefix,uint64_t nonce,
                                const uint8_t* target,const uint8_t* device_seal){
    uint8_t expected[32];
    tao_seal(prefix,nonce,expected);
    return std::memcmp(expected,device_seal,32)==0 && tao_meets_target(expected,target);
}
