/*
 * Native C++ Ascend TAO PoW range scanner.
 * Runs persistent ACL contexts and a native thread per physical device.
 * No runtime interpreter, JSON, subprocesses, wallet keys, or networking on
 * the compute hot path. Feeds public challenges from a trusted caller.
 *
 * Wire protocol on stdin/stdout (one command per line):
 *   SCAN <123-byte-prefix-hex> <difficulty-u64> <start-u64> <count-per-device>
 *   PING
 *   QUIT
 * Response: HIT <nonce> <seal-hex> <attempted> | MISS <attempted> |
 *           ERR <reason> | PONG | BYE
 *
 * max-job-ms bounds stale-challenge latency between non-preemptible kernels.
 */
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "tao_hash_core.h"

#include "tao_api.h"
namespace {
constexpr uint64_t kMaxAttempts = 0x100000000ULL;
constexpr uint32_t kMaxBatch=1048576U;
constexpr char kDomain[]="subtensor-pow-register-v1";
struct Device {
    int id; void* ctx=nullptr;
    Device(int i, uint32_t grid):id(i) {
        int status=tao_create(i,grid,&ctx);
        if (status || !ctx) throw std::runtime_error("tao_create status="+std::to_string(status));
    }
    ~Device(){if(ctx)tao_destroy(ctx);}
    Device(const Device&)=delete;
    Device& operator=(const Device&)=delete;
};
struct Config {
    std::vector<int> ids={0};
    uint32_t grid=40,batch=8192;
    uint64_t max_job_ms=6000;
};
uint64_t u64(const std::string& s){
    if(s.empty() || s[0]=='-' || s[0]=='+') throw std::invalid_argument("invalid unsigned integer");
    size_t pos=0;
    unsigned long long v=std::stoull(s,&pos,10);
    if(pos!=s.size()) throw std::invalid_argument("invalid unsigned integer");
    return static_cast<uint64_t>(v);
}
int hex_nibble(char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    throw std::invalid_argument("non-hex character");
}
std::vector<uint8_t> unhex(const std::string& h,size_t n){
    if(h.size()!=2*n)throw std::invalid_argument("wrong prefix hex length");
    std::vector<uint8_t> out(n);
    for(size_t i=0;i<n;++i)out[i]=static_cast<uint8_t>((hex_nibble(h[2*i])<<4)|hex_nibble(h[2*i+1]));
    return out;
}
std::string hex32(const uint8_t* b){
    std::ostringstream ss;
    ss<<std::hex<<std::setfill('0');
    for(int i=0;i<32;++i)ss<<std::setw(2)<<static_cast<unsigned>(b[i]);
    return ss.str();
}
std::vector<int> parse_devices(const std::string& str){
    std::vector<int> v;
    std::istringstream ss(str);std::string s;
    while(std::getline(ss,s,',')){
        uint64_t n=u64(s);
        if(n>255)throw std::invalid_argument("device id exceeds 255");
        int idx=static_cast<int>(n);
        if(std::find(v.begin(),v.end(),idx)!=v.end())throw std::invalid_argument("duplicate device");
        v.push_back(idx);
    }
    if(v.empty()||v.size()>32)throw std::invalid_argument("requires 1..32 devices");
    return v;
}
// Divide the 256-bit little-endian (2^256-1) by a u64 difficulty, without
// multiprecision dependencies or floating-point loss.
void target_from_difficulty(uint64_t difficulty, uint8_t out[32]){
    if(!difficulty)throw std::invalid_argument("difficulty must be >0");
    uint64_t rem=0;
    for(int limb=3;limb>=0;--limb){
        const unsigned __int128 value=(static_cast<unsigned __int128>(rem)<<64)|UINT64_MAX;
        const uint64_t quotient=static_cast<uint64_t>(value/difficulty);
        rem=static_cast<uint64_t>(value%difficulty);
        for(int b=0;b<8;++b)out[limb*8+b]=static_cast<uint8_t>(quotient>>(8*b));
    }
}
void validate_prefix(const std::vector<uint8_t>& p){
    static constexpr size_t domainlen=sizeof(kDomain)-1;
    if(p.size()!=123 || std::memcmp(p.data(),kDomain,domainlen)!=0 ||
       (p[domainlen]==0 && p[domainlen+1]==0))
        throw std::invalid_argument("invalid non-root registration challenge");
}
void print_help(){
    std::cerr << "Usage: tao_native [--devices 0,1] [--grid 40] [--batch 8192]"
              << " [--max-job-ms 6000]\n"
              << "stdin: SCAN <123-byte-prefix-hex> <u64-difficulty> <u64-start> <count-per-device>\n"
              << "       PING | QUIT\n";
}
Config args(int argc,char** argv){
    Config cfg;
    for(int i=1;i<argc;++i){
        const std::string key=argv[i];
        if(key=="--help"){print_help();std::exit(0);}
        if(i+1>=argc)throw std::invalid_argument("missing value for "+key);
        const std::string val=argv[++i];
        if(key=="--devices")cfg.ids=parse_devices(val);
        else if(key=="--grid")cfg.grid=static_cast<uint32_t>(u64(val));
        else if(key=="--batch")cfg.batch=static_cast<uint32_t>(u64(val));
        else if(key=="--max-job-ms")cfg.max_job_ms=u64(val);
        else throw std::invalid_argument("unknown option: "+key);
    }
    if(cfg.grid<1||cfg.grid>80)throw std::invalid_argument("grid must be 1..80");
    if(cfg.batch<1||cfg.batch>kMaxBatch)throw std::invalid_argument("batch must be 1..1048576");
    if(cfg.max_job_ms<50||cfg.max_job_ms>60000)throw std::invalid_argument("max-job-ms must be 50..60000");
    return cfg;
}
struct SharedResult{
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> scanned{0};
    std::mutex lock;
    bool hit=false;
    uint64_t nonce=0;
    uint8_t seal[32]={0};
    int failed=0;
};
void scan_one(Device& device, const uint8_t* prefix,const uint8_t* target,
    uint64_t start,uint64_t count,uint32_t batch,
    std::chrono::steady_clock::time_point end, SharedResult& result){
    uint64_t done=0;
    while(done<count && !result.stop.load(std::memory_order_relaxed)){
        if(std::chrono::steady_clock::now()>=end)break;
        uint64_t left=count-done;
        uint32_t n=static_cast<uint32_t>(std::min<uint64_t>(left,batch));
        uint64_t answer=0;int found=0;uint8_t seal[32]={0};
        const int rc=tao_mine(device.ctx,prefix,target,start+done,n,&answer,&found,seal);
        if(rc){
            {std::lock_guard<std::mutex> guard(result.lock);result.failed=rc;}
            result.stop.store(true,std::memory_order_release);
            return;
        }
        result.scanned.fetch_add(n,std::memory_order_relaxed);
        if(found){
            std::lock_guard<std::mutex> guard(result.lock);
            if(!result.hit){
                result.hit=true;result.nonce=answer;std::memcpy(result.seal,seal,32);
            }
            result.stop.store(true,std::memory_order_release);
            return;
        }
        done+=n;
    }
}
void process_scan(std::istringstream& cmd,const Config& cfg,
                  const std::vector<std::unique_ptr<Device>>& devices){
    std::string challenge_text,diff_text,start_text,count_text,extra;
    if(!(cmd>>challenge_text>>diff_text>>start_text>>count_text) || (cmd>>extra))
        throw std::invalid_argument("SCAN needs exactly 4 arguments");
    const std::vector<uint8_t> prefix=unhex(challenge_text,123);
    validate_prefix(prefix);
    const uint64_t difficulty=u64(diff_text),start=u64(start_text),count=u64(count_text);
    if(count<1 || count>kMaxAttempts)throw std::invalid_argument("count per device must be 1..4294967296");
    uint8_t target[32];target_from_difficulty(difficulty,target);
    SharedResult result;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(cfg.max_job_ms);
    std::vector<std::thread> threads;
    threads.reserve(devices.size());
    for(size_t i=0;i<devices.size();++i){
        // Disjoint modulo-u64 ranges, with natural wraparound on overflow.
        const uint64_t device_start=start+static_cast<uint64_t>(i)*count;
        threads.emplace_back(scan_one,std::ref(*devices[i]),prefix.data(),target,
            device_start,count,cfg.batch,deadline,std::ref(result));
    }
    for(auto& thread:threads)thread.join();
    if(result.failed)throw std::runtime_error("ACL/kernel status="+std::to_string(result.failed));
    const uint64_t scanned=result.scanned.load(std::memory_order_relaxed);
    if(result.hit)std::cout<<"HIT "<<result.nonce<<" "<<hex32(result.seal)<<" "<<scanned<<std::endl;
    else std::cout<<"MISS "<<scanned<<std::endl;
}
}
int main(int argc,char** argv){
    try{
        const Config cfg=args(argc,argv);
        if(tao_api_version()!=3)throw std::runtime_error("native library ABI mismatch (want v3)");
        std::vector<std::unique_ptr<Device>> devices;
        for(int id:cfg.ids)devices.emplace_back(new Device(id,cfg.grid));
        std::string line;
        while(std::getline(std::cin,line)){
            try{
                std::istringstream ss(line);
                std::string verb;ss>>verb;
                if(verb=="QUIT"){std::cout<<"BYE"<<std::endl;return 0;}
                if(verb=="PING"){std::cout<<"PONG"<<std::endl;continue;}
                if(verb=="SCAN"){process_scan(ss,cfg,devices);continue;}
                if(verb.empty())continue;
                std::cout<<"ERR unknown command"<<std::endl;
            }catch(const std::exception& e){
                // In particular an ACL/runtime failure must not be swallowed.
                std::cout<<"ERR "<<e.what()<<std::endl;
                return 2;
            }
        }
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"fatal: "<<e.what()<<std::endl;
        return 1;
    }
}
