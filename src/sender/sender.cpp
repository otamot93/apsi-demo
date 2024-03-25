//
// Created by mac on 2024/2/29.
//

// std
#include <iostream>
#include <fstream>
#include <signal.h>

// absl
#include <absl/log/log.h>

// apsi
#include <apsi/thread_pool_mgr.h>
#include <apsi/oprf/oprf_sender.h>
#include <apsi/oprf/oprf_common.h>
#include <apsi/sender.h>
#include <apsi/zmq/sender_dispatcher.h>

// common
# include "common/csv_reader.h"

using namespace std;

// apsi
using namespace apsi;
using namespace apsi::sender;
using namespace apsi::oprf;


int startSender();
shared_ptr<SenderDB> try_load_csv_db(OPRFKey &oprf_key);

unique_ptr<PSIParams> build_psi_param();

/**
 * 加载csv文件
 * @param db_file
 * @return
 */
unique_ptr<CSVReader::DBData> load_db(string & db_file);

/**
 * 创建sender_db
 * @param db_data
 * @param psi_params
 * @param oprf_key
 * @param nonce_byte_count
 * @param compress
 * @return
 */
shared_ptr<SenderDB> create_sender_db(
        const CSVReader::DBData &db_data,
        unique_ptr<PSIParams> psi_params,
        OPRFKey &oprf_key,
        size_t nonce_byte_count,
        bool compress
        );


void sigint_handle(int param [[maybe_unused]]){
    LOG(WARNING) << "Sender interupted";
    exit(0);

}

int main(){

    signal(SIGINT,sigint_handle);
    startSender();
    return 0;
}


int startSender(){
    ThreadPoolMgr::SetThreadCount(10);
    LOG(INFO) << "setting thread to " << ThreadPoolMgr::GetThreadCount();

    OPRFKey oprf_key;
    shared_ptr<SenderDB> sender_db = try_load_csv_db(oprf_key);

    // 打印bin bundles相关数据
    uint32_t  max_bin_bundles_per_bundle_idx = 0;
    for(uint32_t bundle_idx = 0;bundle_idx < sender_db ->get_params().bundle_idx_count();bundle_idx++){
        max_bin_bundles_per_bundle_idx = std::max(max_bin_bundles_per_bundle_idx,static_cast<uint32_t>(sender_db->get_bin_bundle_count(bundle_idx)));
    }
    LOG(INFO) << "SenderDB holds a total of " << sender_db->get_bin_bundle_count() << " ; bin bundles across " << sender_db->get_params().bundle_idx_count() << " bundle indices";
    LOG(INFO) << "The largest bundle index holds " << max_bin_bundles_per_bundle_idx << " bin bundles";

    // 存储sender_db

    // 运行服务
    atomic<bool> stop = false;
    ZMQSenderDispatcher dispatch(sender_db,oprf_key);

    dispatch.run(stop,1212);
    return 0;
}

// 从csv中加载db
shared_ptr<SenderDB> try_load_csv_db(OPRFKey &oprf_key){
    unique_ptr<PSIParams> params = build_psi_param();
    if(!params){
        LOG(ERROR) << "Failed to get params";
        return nullptr;
    }

    unique_ptr<CSVReader::DBData> db_data;
    string db_file_path = "/Users/mac/Documents/workspace/cpp/apsi-test/data/db.csv";
    if(!(db_data = load_db(db_file_path))){
        LOG(ERROR) << "load db error";
        return nullptr;
    }


    return create_sender_db(*db_data,std::move(params),oprf_key,16,false) ;
}

unique_ptr<PSIParams> build_psi_param(){
    string params_json;

    try{
        fstream input_file("/Users/mac/Documents/workspace/cpp/apsi-test/data/params.json",ios_base::in);
        if(!input_file.is_open()){
            APSI_LOG_ERROR("params file could not be open for read")
            throw runtime_error("count not open params file");
        }
        string line;
        while(getline(input_file,line)){
            params_json.append(line);
            params_json.append("\n");
        }
        LOG(INFO) << "param file content:" << endl << params_json ;
    }catch(const exception &ex){
        APSI_LOG_ERROR("error trying to read input file " << ex.what())
        return nullptr;
    }

    unique_ptr<PSIParams> params;
    try{
        params = make_unique<PSIParams>(PSIParams::Load(params_json));
    }catch(const exception &ex){
        APSI_LOG_ERROR("error create params" << ex.what());
        return nullptr;
    }

    APSI_LOG_INFO("PSIParams have false-positive probability 2^(" << params->log2_fpp()
                                                                  << ") per receiver item")

    return params;
}

/**
 * 加载csv文件
 * @param db_file
 * @return
 */
unique_ptr<CSVReader::DBData> load_db(string & db_file){
    CSVReader::DBData  db_data;
    try{
        CSVReader csv_reader(db_file);
        tie(db_data,ignore) = csv_reader.read();

    }catch(exception &ex){
        LOG(ERROR) << "read csv error" << ex.what();
        return nullptr;
    }

    return make_unique<CSVReader::DBData>(std::move(db_data));
}

shared_ptr<SenderDB> create_sender_db(
        const CSVReader::DBData &db_data,
        unique_ptr<PSIParams> psi_params,
        OPRFKey &oprf_key,
        size_t nonce_byte_count,
        bool compress
){
    if(!psi_params){
        LOG(ERROR) << "No PSI parameter was given";
    }

    shared_ptr<SenderDB> sender_db;
    if(holds_alternative<CSVReader::UnlabeledData>(db_data)){
        try{
            sender_db = make_shared<SenderDB>(*psi_params,0,0,compress);
            sender_db->set_data(get<CSVReader::UnlabeledData>(db_data));
        }catch(exception &ex){
            LOG(ERROR) << "Failed to create SenderDb:" << ex.what();
            return nullptr;
        }
    }else if(holds_alternative<CSVReader::LabeledData>(db_data)){
        // labeled psi 先不处理
        LOG(ERROR) << "Not Support Labeled Psi" ;
        return nullptr;
    }else{
        LOG(ERROR) << "UnKnown database state";
        return  nullptr;
    }
    if(compress) {
        LOG(INFO) << "Using in-memory compression to reduce memory footprint";
    }

    oprf_key = sender_db->strip();

    LOG(INFO) << "SenderDB packing rate: " << sender_db->get_packing_rate();

    return sender_db;
}
