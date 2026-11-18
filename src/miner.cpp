/*
 * Stratum V1 SHA256d miner for TAOHash BTC pool.
 * Standalone C++17: no Python, no test harness, no blockchain secret keys.
 * MIT license.  Pool shares are SHA256d, not Bittensor registration PoW.
 */
#include "sha256d_core.h"
#include "th_npu_api.h"
#include <json-c/json.h>
#include <openssl/sha.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <dlfcn.h>
#include <errno.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using Clock=std::chrono::steady_clock;
using boost::multiprecision::cpp_int;
static std::atomic<bool> keep_running{true};
static void request_stop(int){keep_running.store(false);}
static void log(const std::string &s) {
    static std::mutex l;
    std::lock_guard<std::mutex> hold(l);
    auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[32];std::strftime(buf,sizeof(buf),"%F %T",std::localtime(&now));
    std::cerr<<'['<<buf<<"] "<<s<<'\n';
}
static int digit(char ch){if(ch>='0'&&ch<='9')return ch-'0';if(ch>='a'&&ch<='f')return ch-'a'+10;if(ch>='A'&&ch<='F')return ch-'A'+10;return -1;}
static std::vector<uint8_t> from_hex(const std::string &hex){
    if(hex.size()%2)throw std::runtime_error("odd-length hex");
    std::vector<uint8_t> out(hex.size()/2);
    for(size_t i=0;i<out.size();++i){int a=digit(hex[i*2]),b=digit(hex[i*2+1]);if(a<0||b<0)throw std::runtime_error("invalid hex");out[i]=(uint8_t)((a<<4)|b);}
    return out;
}
static std::string to_hex(const uint8_t *src,size_t n){
    constexpr char h[]="0123456789abcdef";
    std::string out(n*2,'0');for(size_t i=0;i<n;++i){out[i*2]=h[src[i]>>4];out[i*2+1]=h[src[i]&15];}return out;
}
static std::string be32(uint32_t value){char s[9];std::snprintf(s,sizeof(s),"%08x",value);return s;}
static uint32_t read_u32_be_hex(const std::string &s){
    if(s.size()!=8)throw std::runtime_error("expected 8-digit 32-bit field");
    auto v=from_hex(s);return ((uint32_t)v[0]<<24)|((uint32_t)v[1]<<16)|((uint32_t)v[2]<<8)|v[3];
}
static void put_le32(uint8_t *dst,uint32_t x){for(int i=0;i<4;++i)dst[i]=(uint8_t)(x>>(i*8));}
static void double_sha(const uint8_t *data,size_t n,uint8_t out[32]){
    uint8_t x[32];SHA256(data,n,x);SHA256(x,32,out);
}
static std::array<uint8_t,32> diff_target(double difficulty){
    if(!std::isfinite(difficulty)||difficulty<=0.0)throw std::runtime_error("invalid pool difficulty");
    // Fixed-point conversion avoids overflowing floating point with a 256-bit target.
    std::ostringstream ss;ss<<std::fixed<<std::setprecision(10)<<difficulty;
    std::string s=ss.str();auto dot=s.find('.');int decimals=0;
    if(dot!=std::string::npos){decimals=(int)(s.size()-dot-1);s.erase(dot,1);}
    cpp_int numerator=0;for(char c:s){if(c<'0'||c>'9')throw std::runtime_error("invalid pool difficulty");numerator=numerator*10+(c-'0');}
    if(numerator==0)throw std::runtime_error("difficulty below 1e-10");
    cpp_int denominator=1;for(int i=0;i<decimals;++i)denominator*=10;
    const cpp_int diff1("0x00000000FFFF0000000000000000000000000000000000000000000000000000");
    cpp_int target=(diff1*denominator)/numerator;
    const cpp_int limit=(cpp_int(1)<<256)-1;if(target>limit)target=limit;
    std::array<uint8_t,32> result{};
    for(int i=0;i<32;++i){result[i]=(uint8_t)(target&255);target>>=8;}
    return result;
}
static json_object* arr_get(json_object *o,int index){return (o&&json_object_is_type(o,json_type_array))?json_object_array_get_idx(o,index):nullptr;}
static json_object* obj_get(json_object *o,const char *key){json_object *val=nullptr;return o&&json_object_object_get_ex(o,key,&val)?val:nullptr;}
static std::string str(json_object *o){if(!o||!json_object_is_type(o,json_type_string))throw std::runtime_error("Stratum field is not a string");return json_object_get_string(o);}
static json_object* json_str(const std::string &s){return json_object_new_string_len(s.c_str(),(int)s.size());}

struct Job {
    std::string id, prev, coinb1,coinb2, version,bits,ntime;
    std::vector<std::string> branch;
    bool clean=true;
};
struct Snapshot {
    Job job;
    std::string extranonce1;
    int extranonce2_size=0;
    std::array<uint8_t,32> target{};
    double difficulty=1.0;
    uint64_t generation=0,session=0;
    bool active=false;
};
struct Share {
    uint64_t session=0;
    std::string job, ex2, ntime, nonce;
    std::string hash;
};
struct Settings {
    std::string pool="btc.taohash.com",port="3331",user,pass="x",backend="auto",library;
    int device=0,grid=40,reconnect_seconds=5;
    uint32_t chunk=65536;
    bool verbose=false;
};
static void help(){
    std::cout<<"ascend_tao Stratum V1 / SHA256d BTC pool miner (C++17)\n"
             <<"  --user <payout_address.hotkey_slug-worker_id>   REQUIRED\n"
             <<"  --pool btc.taohash.com --port 3331 --pass x\n"
             <<"  --backend auto|ascend|cpu  --device 0 --grid 40\n"
             <<"  --chunk 65536 --npu-lib /path/to/libtaohash_npu.so\n"
             <<"  --reconnect 5 --verbose\n"
             <<"  No Bittensor keys and no Python runtime are needed to run this miner.\n";
}
static Settings cli(int argc,char **argv){
    Settings opt;
    for(int i=1;i<argc;++i){
        std::string k=argv[i];if(k=="--help"||k=="-h"){help();std::exit(0);}
        if(i+1>=argc)throw std::runtime_error("missing value for "+k);
        std::string v=argv[++i];
        if(k=="--user")opt.user=v;else if(k=="--pool")opt.pool=v;
        else if(k=="--port")opt.port=v;else if(k=="--pass")opt.pass=v;
        else if(k=="--backend")opt.backend=v;else if(k=="--npu-lib")opt.library=v;
        else if(k=="--device")opt.device=std::stoi(v);else if(k=="--grid")opt.grid=std::stoi(v);
        else if(k=="--chunk")opt.chunk=(uint32_t)std::stoul(v);
        else if(k=="--reconnect")opt.reconnect_seconds=std::stoi(v);
        else if(k=="--verbose") {opt.verbose=(v=="1"||v=="true");}
        else throw std::runtime_error("unknown argument "+k);
    }
    if(opt.user.empty())throw std::runtime_error("--user required: BTC payout_address.hotkey_slug");
    if(opt.user.find("...")!=std::string::npos)throw std::runtime_error("use a full payout address; not bc1q...");
    if(opt.pool.empty()||opt.port.empty())throw std::runtime_error("empty pool or port");
    if(opt.backend!="auto"&&opt.backend!="ascend"&&opt.backend!="cpu")throw std::runtime_error("backend must be auto, ascend or cpu");
    if(opt.grid<1||opt.grid>80||opt.device<0||opt.chunk<1||opt.chunk>1048576||opt.reconnect_seconds<1)throw std::runtime_error("invalid grid, device, chunk or reconnect");
    return opt;
}
static std::array<uint8_t,80> build_header(const Snapshot &s,const std::vector<uint8_t>&ex2){
    const Job &j=s.job;
    auto cb1=from_hex(j.coinb1),cb2=from_hex(j.coinb2),ex1=from_hex(s.extranonce1);
    std::vector<uint8_t> coinbase;
    coinbase.reserve(cb1.size()+ex1.size()+ex2.size()+cb2.size());
    coinbase.insert(coinbase.end(),cb1.begin(),cb1.end());
    coinbase.insert(coinbase.end(),ex1.begin(),ex1.end());
    coinbase.insert(coinbase.end(),ex2.begin(),ex2.end());
    coinbase.insert(coinbase.end(),cb2.begin(),cb2.end());
    std::array<uint8_t,32> root{};double_sha(coinbase.data(),coinbase.size(),root.data());
    for(const auto &b:j.branch){
        auto pair=from_hex(b);if(pair.size()!=32)throw std::runtime_error("invalid merkle branch");
        uint8_t data[64];std::memcpy(data,root.data(),32);std::memcpy(data+32,pair.data(),32);
        double_sha(data,64,root.data());
    }
    auto prev=from_hex(j.prev);if(prev.size()!=32)throw std::runtime_error("invalid prevhash size");
    std::array<uint8_t,80> header{};
    put_le32(header.data(),read_u32_be_hex(j.version));
    // Bitcoin Stratum V1 previous hash is word-swapped, so undo the byte
    // reversal *within* each 4-byte word (do not reverse all 32 bytes).
    for(int i=0;i<32;i+=4)for(int q=0;q<4;++q)header[4+i+q]=prev[i+3-q];
    std::memcpy(header.data()+36,root.data(),32);
    put_le32(header.data()+68,read_u32_be_hex(j.ntime));
    put_le32(header.data()+72,read_u32_be_hex(j.bits));
    return header;
}

class Scanner {
    void *lib=nullptr,*ctx=nullptr;
    using NewCtx=int (*)(int,uint32_t,void**);
    using Scan=int (*)(void*,const uint8_t*,const uint8_t*,uint32_t,uint32_t,uint32_t*,int*,uint8_t*);
    using Destroy=void (*)(void*);
    NewCtx init=nullptr;Scan scan=nullptr;Destroy destroy=nullptr;
    bool ascend=false;
    std::atomic<uint64_t> total{0};
public:
    explicit Scanner(const Settings &s){
        if(s.backend=="cpu") {log("CPU SHA256d backend explicitly selected");return;}
        std::string p=s.library;
        if(p.empty()) {
            char exe[4096];const ssize_t n=readlink("/proc/self/exe",exe,sizeof(exe)-1);
            if(n>0) {
                exe[n]=0;std::string path(exe);
                const auto slash=path.find_last_of('/');
                p=path.substr(0,slash)+"/libtaohash_npu.so";
            }else p="./libtaohash_npu.so";
        }
        lib=dlopen(p.c_str(),RTLD_NOW|RTLD_LOCAL);
        if(lib){
            init=(NewCtx)dlsym(lib,"th_create");scan=(Scan)dlsym(lib,"th_scan");destroy=(Destroy)dlsym(lib,"th_destroy");
            if(init&&scan&&destroy){
                int rc=init(s.device,(uint32_t)s.grid,&ctx);
                if(rc==0&&ctx) {ascend=true;log("Ascend AIV backend: "+p+" device="+std::to_string(s.device)+" grid="+std::to_string(s.grid));}
                else log("Ascend initialization failed, ACL error="+std::to_string(rc));
            } else log("NPU library missing expected th_* symbols");
        }else log(std::string("NPU plugin load failed: ")+dlerror());
        if(!ascend){
            if(s.backend=="ascend")throw std::runtime_error("requested --backend ascend is unavailable");
            if(ctx&&destroy)destroy(ctx);
            ctx=nullptr;
            if(lib)dlclose(lib);
            lib=nullptr;
            log("FALLBACK: CPU backend (not NPU hashing)");
        }
    }
    ~Scanner(){if(ctx&&destroy)destroy(ctx);if(lib)dlclose(lib);}
    bool find(const std::array<uint8_t,80>&hdr,const std::array<uint8_t,32>&target,
              uint32_t begin,uint32_t count,uint32_t &nonce,uint8_t digest[32]){
        if(ascend){
            int found=0;int rc=scan(ctx,hdr.data(),target.data(),begin,count,&nonce,&found,digest);
            if(rc)throw std::runtime_error("th_scan ACL/verification error "+std::to_string(rc));
            total.fetch_add(count,std::memory_order_relaxed);
            return found!=0;
        }
        uint32_t mid[8];uint8_t tail[64];th_midstate(hdr.data(),mid,tail);
        for(uint32_t i=0;i<count;++i){
            uint32_t n=begin+i;th_hash_midstate(mid,tail,n,digest);
            if(th_under_target(digest,target.data())){
                nonce=n;total.fetch_add(i+1,std::memory_order_relaxed);return true;
            }
        }
        total.fetch_add(count,std::memory_order_relaxed);return false;
    }
    uint64_t hashes()const{return total.load(std::memory_order_relaxed);}
};

class Pool {
    Settings conf;
    std::mutex mutex;
    Snapshot state;
    bool authorized=false,subscribed=false;
    double next_diff=1.0;
    std::deque<Share> submissions;
    int next_id=100;
    uint64_t num_accepted=0,num_rejected=0;
public:
    explicit Pool(Settings settings):conf(std::move(settings)){}
    Snapshot snapshot(){std::lock_guard<std::mutex> hold(mutex);return state;}
    void disconnect(){
        std::lock_guard<std::mutex> hold(mutex);
        ++state.session; ++state.generation;
        state.active=false;subscribed=false;authorized=false;
        submissions.clear();next_diff=1.0;
    }
    void connected(){
        std::lock_guard<std::mutex> hold(mutex);
        ++state.session; ++state.generation;
        state.active=false;subscribed=false;authorized=false;
        submissions.clear();next_diff=1.0;
    }
    void post(Share sh){
        std::lock_guard<std::mutex> hold(mutex);
        if(sh.session==state.session && state.active && submissions.size()<128)submissions.push_back(std::move(sh));
    }
    std::deque<Share> take(){
        std::lock_guard<std::mutex> hold(mutex);
        std::deque<Share> tmp;tmp.swap(submissions);return tmp;
    }
    uint64_t accepted(){std::lock_guard<std::mutex> h(mutex);return num_accepted;}
    uint64_t rejected(){std::lock_guard<std::mutex> h(mutex);return num_rejected;}
    void process(json_object *msg,std::unordered_map<int,Share>&outstanding,std::vector<std::string>&outbound){
        json_object *method=obj_get(msg,"method"),*params=obj_get(msg,"params"),*id=obj_get(msg,"id");
        if(method && json_object_is_type(method,json_type_string)){
            const std::string m=str(method);
            if(m=="mining.set_difficulty"){
                json_object *v=arr_get(params,0);if(!v)throw std::runtime_error("set_difficulty empty");
                const double d=json_object_get_double(v);(void)diff_target(d);
                std::lock_guard<std::mutex> hold(mutex);next_diff=d;
                log("pool next-job difficulty="+std::to_string(d));
            }else if(m=="mining.set_extranonce"){
                std::string ex1=str(arr_get(params,0));int ex2=json_object_get_int(arr_get(params,1));
                if(ex2<1||ex2>16||from_hex(ex1).size()>32)throw std::runtime_error("bad extranonce");
                std::lock_guard<std::mutex> hold(mutex);
                state.extranonce1=ex1;state.extranonce2_size=ex2;state.active=false;++state.generation;
                log("pool changed extranonce; awaiting next job");
            }else if(m=="mining.notify"){
                if(!params||json_object_array_length(params)<9)throw std::runtime_error("short mining.notify");
                Job j; j.id=str(arr_get(params,0));j.prev=str(arr_get(params,1));
                j.coinb1=str(arr_get(params,2));j.coinb2=str(arr_get(params,3));
                auto *branches=arr_get(params,4);if(!branches||!json_object_is_type(branches,json_type_array))throw std::runtime_error("bad merkle path");
                for(size_t k=0;k<json_object_array_length(branches);++k)j.branch.push_back(str(arr_get(branches,(int)k)));
                j.version=str(arr_get(params,5));j.bits=str(arr_get(params,6));j.ntime=str(arr_get(params,7));
                j.clean=json_object_get_boolean(arr_get(params,8));
                (void)read_u32_be_hex(j.version);(void)read_u32_be_hex(j.bits);(void)read_u32_be_hex(j.ntime);
                if(j.id.size()>256||j.prev.size()!=64||j.coinb1.size()>16384||j.coinb2.size()>16384||j.branch.size()>64)throw std::runtime_error("bad job sizes");
                std::lock_guard<std::mutex> hold(mutex);
                state.job=std::move(j);state.difficulty=next_diff;state.target=diff_target(next_diff);
                state.active=authorized&&subscribed&&state.extranonce2_size>0;
                ++state.generation;
                if(state.job.clean)submissions.clear();
                log("job "+state.job.id+" difficulty="+std::to_string(state.difficulty)+(state.active?" ACTIVE":" pending authorization"));
            }else if(m=="client.reconnect"){
                log("server requested reconnect");
                throw std::runtime_error("pool requested reconnect");
            }else if(m=="mining.set_version_mask"){
                log("version rolling requested; unchanged version is used");
            }else if(conf.verbose){log("pool method "+m);}
            return;
        }
        if(!id || json_object_is_type(id,json_type_null))return;
        const int rid=json_object_get_int(id);
        json_object *error=obj_get(msg,"error"),*result=obj_get(msg,"result");
        bool has_error=error&&!json_object_is_type(error,json_type_null);
        if(rid==1){
            if(has_error)throw std::runtime_error("subscribe rejected: "+std::string(json_object_to_json_string(error)));
            if(!result||!json_object_is_type(result,json_type_array)||json_object_array_length(result)<3)throw std::runtime_error("invalid subscribe response");
            std::string ex1=str(arr_get(result,1));int ex2=json_object_get_int(arr_get(result,2));
            if(ex2<1||ex2>16||from_hex(ex1).size()>32)throw std::runtime_error("bad subscribe extranonce");
            {
                std::lock_guard<std::mutex> hold(mutex);
                state.extranonce1=ex1;state.extranonce2_size=ex2;subscribed=true; ++state.generation;
            }
            json_object *req=json_object_new_object(),*arr=json_object_new_array();
            json_object_object_add(req,"id",json_object_new_int(2));
            json_object_object_add(req,"method",json_str("mining.authorize"));
            json_object_array_add(arr,json_str(conf.user));json_object_array_add(arr,json_str(conf.pass));
            json_object_object_add(req,"params",arr);
            outbound.push_back(std::string(json_object_to_json_string_ext(req,JSON_C_TO_STRING_PLAIN))+"\n");
            json_object_put(req);log("subscribed, authorizing wallet/hotkey worker");
        }else if(rid==2){
            if(has_error||!json_object_get_boolean(result))throw std::runtime_error("pool rejected mining.authorize");
            std::lock_guard<std::mutex> hold(mutex);
            authorized=true;
            state.active=subscribed&&state.extranonce2_size>0&&!state.job.id.empty();
            ++state.generation;
            log("pool AUTHORIZED; "+std::string(state.active?"mining":"waiting for notify"));
        }else if(rid>=100){
            auto it=outstanding.find(rid);if(it==outstanding.end())return;
            bool ok=!has_error&&json_object_get_boolean(result);
            {
                std::lock_guard<std::mutex> hold(mutex);
                if(ok)++num_accepted;else ++num_rejected;
            }
            log(std::string(ok?"SHARE ACCEPTED ":"SHARE REJECTED ")+it->second.hash+
                (has_error?" error="+std::string(json_object_to_json_string(error)):""));
            outstanding.erase(it);
        }
    }
};

static int connect_tcp(const std::string &host,const std::string &port){
    addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;
    addrinfo *resolved=nullptr;
    int err=getaddrinfo(host.c_str(),port.c_str(),&hints,&resolved);
    if(err)throw std::runtime_error(std::string("DNS: ")+gai_strerror(err));
    int fd=-1;
    for(addrinfo *addr=resolved;addr&&keep_running.load();addr=addr->ai_next){
        fd=socket(addr->ai_family,addr->ai_socktype,addr->ai_protocol);if(fd<0)continue;
        int old=fcntl(fd,F_GETFL,0);fcntl(fd,F_SETFL,old|O_NONBLOCK);
        int rc=connect(fd,addr->ai_addr,addr->ai_addrlen);
        if(rc<0&&errno!=EINPROGRESS){close(fd);fd=-1;continue;}
        if(rc<0){pollfd pfd{fd,POLLOUT,0};rc=poll(&pfd,1,5000);if(rc<=0){close(fd);fd=-1;continue;}
            int soerr=0;socklen_t sz=sizeof(soerr);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&soerr,&sz)!=0||soerr){close(fd);fd=-1;continue;}}
        fcntl(fd,F_SETFL,old|O_NONBLOCK);break;
    }
    freeaddrinfo(resolved);
    if(fd<0)throw std::runtime_error("TCP connect failed to "+host+":"+port);
    return fd;
}
static void write_all(int fd,const std::string &txt){
    size_t done=0;while(done<txt.size()&&keep_running.load()){
        ssize_t n=send(fd,txt.data()+done,txt.size()-done,MSG_NOSIGNAL);
        if(n>0){done+=(size_t)n;continue;}
        if(n<0&&errno==EINTR)continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){
            pollfd pfd{fd,POLLOUT,0};int rc=poll(&pfd,1,5000);if(rc>0)continue;
        }
        throw std::runtime_error("socket send failed");
    }
}
static std::string subscribe_request(){
    json_object *req=json_object_new_object(),*arr=json_object_new_array();
    json_object_object_add(req,"id",json_object_new_int(1));
    json_object_object_add(req,"method",json_str("mining.subscribe"));
    json_object_array_add(arr,json_str("ascend_tao/1.0"));
    json_object_object_add(req,"params",arr);
    std::string out=std::string(json_object_to_json_string_ext(req,JSON_C_TO_STRING_PLAIN))+"\n";
    json_object_put(req);return out;
}
static std::string share_request(int id,const std::string &user,const Share &s){
    json_object *req=json_object_new_object(),*arr=json_object_new_array();
    json_object_object_add(req,"id",json_object_new_int(id));
    json_object_object_add(req,"method",json_str("mining.submit"));
    json_object_array_add(arr,json_str(user));json_object_array_add(arr,json_str(s.job));
    json_object_array_add(arr,json_str(s.ex2));json_object_array_add(arr,json_str(s.ntime));
    json_object_array_add(arr,json_str(s.nonce));json_object_object_add(req,"params",arr);
    std::string out=std::string(json_object_to_json_string_ext(req,JSON_C_TO_STRING_PLAIN))+"\n";
    json_object_put(req);return out;
}
static void network(Pool &pool,const Settings &s){
    while(keep_running.load()){
        int fd=-1;
        try{
            fd=connect_tcp(s.pool,s.port);pool.connected();
            log("TCP connected to "+s.pool+":"+s.port+" (plain Stratum V1)");
            write_all(fd,subscribe_request());
            std::string buffer;
            std::unordered_map<int,Share> outstanding;
            int req_id=100;
            while(keep_running.load()){
                auto batch=pool.take();
                const Snapshot snapshot=pool.snapshot();
                for(auto &share:batch){
                    if(share.session!=snapshot.session)continue;
                    const int id=req_id++;
                    write_all(fd,share_request(id,s.user,share));
                    outstanding.emplace(id,share);
                    log("SHARE SUBMITTED job="+share.job+" nonce="+share.nonce+" hash="+share.hash);
                }
                pollfd pfd{fd,POLLIN,0};int rc=poll(&pfd,1,200);
                if(rc<0){if(errno==EINTR)continue;throw std::runtime_error("socket poll failed");}
                if(rc==0)continue;
                if(pfd.revents&(POLLHUP|POLLERR|POLLNVAL))throw std::runtime_error("socket disconnected");
                if(!(pfd.revents&POLLIN))continue;
                char bytes[8192];ssize_t n=recv(fd,bytes,sizeof(bytes),0);
                if(n<=0){if(n<0&&(errno==EAGAIN||errno==EINTR))continue;throw std::runtime_error("pool closed TCP connection");}
                buffer.append(bytes,(size_t)n);
                if(buffer.size()>8*1024*1024)throw std::runtime_error("oversized pool response");
                size_t pos;
                while((pos=buffer.find('\n'))!=std::string::npos){
                    std::string line=buffer.substr(0,pos);buffer.erase(0,pos+1);
                    if(line.empty())continue;
                    json_tokener *tok=json_tokener_new();
                    json_object *message=json_tokener_parse_ex(tok,line.c_str(),(int)line.size());
                    json_tokener_error jerr=json_tokener_get_error(tok);json_tokener_free(tok);
                    if(jerr!=json_tokener_success||!message){log("ignoring bad JSON from pool");continue;}
                    std::vector<std::string> sends;
                    try{pool.process(message,outstanding,sends);}catch(...){json_object_put(message);throw;}
                    json_object_put(message);
                    for(const auto &payload:sends)write_all(fd,payload);
                }
            }
        }catch(const std::exception &e){log(std::string("network: ")+e.what());}
        if(fd>=0)close(fd);
        pool.disconnect();
        for(int i=0;i<s.reconnect_seconds*10&&keep_running.load();++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
static uint64_t seed_counter(){
    std::random_device rd;uint64_t x=(((uint64_t)rd())<<32)^rd();
    return x^((uint64_t)Clock::now().time_since_epoch().count());
}
static void miner(Pool &pool,Scanner &scanner,const Settings &settings){
    uint64_t active_generation=~uint64_t(0);
    uint64_t ex_nonce=seed_counter(),cursor=0;
    uint64_t current_ex=0;
    Snapshot snap;std::array<uint8_t,80> header{};
    auto last_report=Clock::now();uint64_t last_hashes=0;
    while(keep_running.load()){
        Snapshot incoming=pool.snapshot();
        if(!incoming.active){
            active_generation=~uint64_t(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;
        }
        if(incoming.generation!=active_generation){
            snap=incoming;active_generation=incoming.generation;
            cursor=0;current_ex=++ex_nonce;
            try{
                std::vector<uint8_t> extranonce((size_t)snap.extranonce2_size);
                for(size_t i=0;i<extranonce.size();++i)extranonce[i]=(uint8_t)(current_ex>>(8*(i%8)));
                header=build_header(snap,extranonce);
            }catch(const std::exception &e){log(std::string("job decode error: ")+e.what());
                active_generation=~uint64_t(0);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));continue;}
            log("mining job="+snap.job.id+" extranonce2 bytes="+std::to_string(snap.extranonce2_size));
        }
        if(cursor>=0x100000000ULL){
            cursor=0;current_ex=++ex_nonce;
            try{
                std::vector<uint8_t> extranonce((size_t)snap.extranonce2_size);
                for(size_t i=0;i<extranonce.size();++i)extranonce[i]=(uint8_t)(current_ex>>(8*(i%8)));
                header=build_header(snap,extranonce);
            }catch(const std::exception &e){log(e.what());break;}
        }
        uint32_t count=(uint32_t)std::min<uint64_t>(settings.chunk,0x100000000ULL-cursor);
        uint32_t found_nonce=0;uint8_t digest[32];bool found=false;
        try{found=scanner.find(header,snap.target,(uint32_t)cursor,count,found_nonce,digest);}
        catch(const std::exception &e){log(std::string("hashing stopped: ")+e.what());keep_running=false;break;}
        cursor+=(uint64_t)count;
        if(found){
            uint8_t verified[32];th_hash_header(header.data(),found_nonce,verified);
            // Independently verify device/host double-SHA using OpenSSL.
            uint8_t headerWithNonce[80];std::memcpy(headerWithNonce,header.data(),80);
            put_le32(headerWithNonce+76,found_nonce);
            uint8_t ref[32];double_sha(headerWithNonce,80,ref);
            if(std::memcmp(ref,verified,32)!=0||std::memcmp(verified,digest,32)!=0||!th_under_target(ref,snap.target.data())){
                log("CRITICAL: candidate SHA256d/target verification failed; no submit");keep_running=false;break;
            }
            Share sh;sh.session=snap.session;sh.job=snap.job.id;sh.ntime=snap.job.ntime;sh.nonce=be32(found_nonce);
            std::vector<uint8_t> extra((size_t)snap.extranonce2_size);
            for(size_t i=0;i<extra.size();++i)extra[i]=(uint8_t)(current_ex>>(8*(i%8)));
            sh.ex2=to_hex(extra.data(),extra.size());
            sh.hash=to_hex(ref,32); // raw SHA256d digest; pool checks it independently
            if(pool.snapshot().generation==snap.generation)pool.post(std::move(sh));
        }
        auto now=Clock::now();if(now-last_report>=std::chrono::seconds(10)){
            double dt=std::chrono::duration<double>(now-last_report).count();
            uint64_t h=scanner.hashes();double rate=(h-last_hashes)/dt;
            log("rate="+std::to_string(rate/1e6)+" MH/s (local scans) shares accepted="+
                std::to_string(pool.accepted())+" rejected="+std::to_string(pool.rejected()));
            last_report=now;last_hashes=h;
        }
    }
}
int main(int argc,char **argv){
    ::signal(SIGINT,request_stop);::signal(SIGTERM,request_stop);
    try{
        Settings s=cli(argc,argv);
        log("TAOHash BTC Stratum V1 SHA256d -- not Bittensor registration PoW");
        log("CPU fallback will have very low probability of finding production-difficulty shares");
        Scanner scanner(s);Pool pool(s);
        std::thread net([&](){network(pool,s);});
        miner(pool,scanner,s);
        keep_running.store(false);net.join();
        log("stopped cleanly; accepted="+std::to_string(pool.accepted())+" rejected="+std::to_string(pool.rejected()));
    }catch(const std::exception &e){std::cerr<<"ERROR: "<<e.what()<<"\n";help();return 1;}
    return 0;
}
