/*
 * Reference implementation of the ORIGINAL Bittensor whitepaper's
 * peer-ranking / sigmoid consensus / incentive / bond accounting.
 * This is NOT the current on-chain Yuma Consensus, not a subnet validator,
 * and not a mining hash function. Standalone C++17, no Python runtime.
 * Whitepaper: https://www.bittensor.com/whitepaper (sections 1-3).
 */
#include <json-c/json.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using V = std::vector<double>;
using M = std::vector<V>;
static json_object* prop(json_object* j,const char* key) {
    json_object *v=nullptr;
    if(!json_object_object_get_ex(j,key,&v)) throw std::runtime_error(std::string("missing JSON field: ")+key);
    return v;
}
static double scalar(json_object* j) {
    if(!json_object_is_type(j,json_type_int) && !json_object_is_type(j,json_type_double)) throw std::runtime_error("expected JSON number");
    double v=json_object_get_double(j);
    if(!std::isfinite(v)) throw std::runtime_error("non-finite number");
    return v;
}
static V vec(json_object* j) {
    if(!json_object_is_type(j,json_type_array)) throw std::runtime_error("expected JSON array");
    V v;
    for(size_t i=0;i<json_object_array_length(j);++i) v.push_back(scalar(json_object_array_get_idx(j,i)));
    return v;
}
static M matrix(json_object* j) {
    if(!json_object_is_type(j,json_type_array)) throw std::runtime_error("expected matrix array");
    M m;
    for(size_t i=0;i<json_object_array_length(j);++i) m.push_back(vec(json_object_array_get_idx(j,i)));
    return m;
}
static json_object *emit(const V& v) {
    json_object *out=json_object_new_array();
    for(double x:v) json_object_array_add(out,json_object_new_double(x));
    return out;
}
static json_object *emit(const M& v) {
    json_object *out=json_object_new_array();
    for(const V& row:v) json_object_array_add(out,emit(row));
    return out;
}
static void put(json_object* dest,const char* key,const V& v) {json_object_object_add(dest,key,emit(v));}
static void put(json_object* dest,const char* key,const M& v) {json_object_object_add(dest,key,emit(v));}
static void check_matrix(const M& m,size_t n,const char* field) {
    if(m.size()!=n) throw std::runtime_error(std::string(field)+" must be NxN");
    for(const auto& row:m) {
        if(row.size()!=n) throw std::runtime_error(std::string(field)+" must be NxN");
        for(double x:row) if(x<0.0) throw std::runtime_error(std::string(field)+" cannot contain negative values");
    }
}
static double logistic(double x) {
    return x>=0.0?1.0/(1.0+std::exp(-x)):std::exp(x)/(1.0+std::exp(x));
}
int main(int argc,char** argv) {
    try {
        if(argc!=2 || std::string(argv[1])=="--help") {
            std::cerr<<"Usage: whitepaper_incentive docs/whitepaper_example.json\n"
                     <<"Original whitepaper math only; NOT live Bittensor/Yuma payout code.\n";
            return argc==2?0:1;
        }
        std::unique_ptr<json_object,decltype(&json_object_put)> input(json_object_from_file(argv[1]),json_object_put);
        if(!input) throw std::runtime_error("cannot parse input JSON file");
        V stake=vec(prop(input.get(),"stake"));
        M weight=matrix(prop(input.get(),"weights"));
        if(stake.empty() || stake.size()>4096) throw std::runtime_error("stake must contain 1..4096 peers");
        const size_t n=stake.size();check_matrix(weight,n,"weights");
        double rho=scalar(prop(input.get(),"rho"));
        double kappa=scalar(prop(input.get(),"kappa"));
        double tau=scalar(prop(input.get(),"tau"));
        if(rho<=0||rho>1e6||kappa<0||kappa>1||tau<0||tau>1) throw std::runtime_error("invalid rho/kappa/tau parameters");
        double sum=0;
        for(double& s:stake){if(s<0) throw std::runtime_error("negative stake");sum+=s;}
        if(!std::isfinite(sum)||sum<=0) throw std::runtime_error("stake total must be positive");
        for(double& s:stake)s/=sum; // stake is a proportion as defined in paper.
        for(auto& row:weight){
            double wsum=0;for(double x:row)wsum+=x;
            if(!std::isfinite(wsum)) throw std::runtime_error("invalid weight row");
            if(wsum>0)for(double& x:row)x/=wsum;
        }
        // Convention: weights[owner][target], bonds[target][owner].
        // Bonds are held by owners and attached to a particular target.
        // Transposing this representation lets us directly apply B^T I.
        M bonds(n,V(n,0.0));
        json_object* old=nullptr;
        if(json_object_object_get_ex(input.get(),"bonds",&old)) {
            bonds=matrix(old);check_matrix(bonds,n,"bonds");
        }
        V ranking(n,0.0),trust(n,0.0),consensus(n,0.0),raw_incentive(n,0.0);
        for(size_t i=0;i<n;++i){
            for(size_t j=0;j<n;++j){
                ranking[j]+=stake[i]*weight[i][j];        // R = W^T S (paper section 1 ranking)
                if(weight[i][j]>0.0)trust[j]+=stake[i];   // T_{i,j}=1 iff W_{i,j}>0
                bonds[j][i]+=weight[i][j]*stake[i];      // delta bonds for target j, owner i
            }
        }
        double isums=0;
        for(size_t j=0;j<n;++j){
            consensus[j]=logistic(rho*(trust[j]-kappa));   // paper Eq 4
            raw_incentive[j]=ranking[j]*consensus[j];     // paper Eq 5
            isums+=raw_incentive[j];
        }
        V incentive=raw_incentive;
        if(isums>0.0)for(double& v:incentive)v/=isums; // allocation shares sum to 1
        // The paper calls b_{i,j} a *fraction*. Normalize accumulated
        // ownership *within each target* before distributing dividends.
        for(auto& owner_row:bonds){
            double total=0;for(double x:owner_row)total+=x;
            if(total>0)for(double& x:owner_row)x/=total;
        }
        V dividends(n,0.0),delta_stake(n,0.0),stake_next(n,0.0);
        for(size_t i=0;i<n;++i){
            for(size_t j=0;j<n;++j)dividends[i]+=bonds[j][i]*incentive[j]; // B^T * I, Eq 8
            delta_stake[i]=0.5*dividends[i]+0.5*incentive[i];           // Eq 8, 50/50 example
            stake_next[i]=stake[i]+tau*delta_stake[i];                // Eq 1 / Eq 8
        }
        std::unique_ptr<json_object,decltype(&json_object_put)> output(json_object_new_object(),json_object_put);
        json_object_object_add(output.get(),"notice",json_object_new_string("Original whitepaper illustration only; NOT on-chain Yuma, not a miner, not an earnings estimator."));
        json_object_object_add(output.get(),"bond_indexing",json_object_new_string("bonds[target][owner]"));
        put(output.get(),"normalized_stake",stake);
        put(output.get(),"normalized_weights_owner_by_target",weight);
        put(output.get(),"rank_W_transpose_S",ranking);
        put(output.get(),"trust_T_transpose_S",trust);
        put(output.get(),"consensus_sigmoid",consensus);
        put(output.get(),"raw_incentive_R_times_C",raw_incentive);
        put(output.get(),"normalized_incentive",incentive);
        put(output.get(),"normalized_bonds_target_by_owner",bonds);
        put(output.get(),"dividend_B_transpose_I",dividends);
        put(output.get(),"delta_stake",delta_stake);
        put(output.get(),"stake_next",stake_next);
        std::cout<<json_object_to_json_string_ext(output.get(),JSON_C_TO_STRING_PRETTY)<<'\n';
    }catch(const std::exception& e) {
        std::cerr<<"WHITEPAPER ERROR: "<<e.what()<<'\n';return 1;
    }
    return 0;
}
