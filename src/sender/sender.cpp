//
// Created by mac on 2024/2/29.
//

// std
#include <iostream>
#include <fstream>
#include <signal.h>

// absl
#include <absl/log/log.h>
#include <absl/flags/parse.h>
#include <absl/flags/flag.h>

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

// absl参数
ABSL_FLAG(uint32_t ,thread,10,"Number of thread");
ABSL_FLAG(std::string,params_path,"./params.json","params file path");
ABSL_FLAG(std::string,db_path,"./db.csv","db file path");
ABSL_FLAG(uint32_t ,noce_byte_count,16,"Number of bytes used for the nonce in labeled mode (default is 16)");
ABSL_FLAG(bool,compress,false,"Whether to compress the SenderDB in memory(default is false)");
ABSL_FLAG(std::string,sdb_output_path,"","The Path of sdb save file(if is not empty)");




int startSender();

/**
 * 从SenderDB文件中读取
 * @param db_path
 * @param oprf_key
 * @return
 */
shared_ptr<SenderDB> try_load_sender_db(string db_path,OPRFKey &oprf_key);

/**
 * 从csv文件中读取
 * @param db_path
 * @param oprf_key
 * @return
 */
shared_ptr<SenderDB> try_load_csv_db(string db_path,OPRFKey &oprf_key);


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

/**
 * 保存sender db
 * @param sdb_output_path
 * @param sender_db
 * @param oprf_key
 * @return
 */
bool try_save_sender_db(const string sdb_output_path,shared_ptr<SenderDB> sender_db,const OPRFKey oprf_key);

void sigint_handle(int param [[maybe_unused]]){
    APSI_LOG_WARNING( "Sender interupted");
    exit(0);

}

int main(int argc,char** argv){
    apsi::Log::SetLogLevel(apsi::Log::Level::all);
    absl::ParseCommandLine(argc,argv);
    string db_path = absl::GetFlag(FLAGS_db_path);
    APSI_LOG_INFO( "Path of db is " << db_path);
    signal(SIGINT,sigint_handle);
    startSender();
    return 0;
}


int startSender(){
    ThreadPoolMgr::SetThreadCount(absl::GetFlag(FLAGS_thread));
    APSI_LOG_INFO("setting thread to " << ThreadPoolMgr::GetThreadCount());

    // sender db 数据或原始csv数据
    string db_path = absl::GetFlag(FLAGS_db_path);
    OPRFKey oprf_key;
    shared_ptr<SenderDB> sender_db;

    bool reload_from_sender_db = false;

    if(!(sender_db = try_load_sender_db(db_path,oprf_key))){
        if(!(sender_db = try_load_csv_db(db_path,oprf_key))){
            APSI_LOG_ERROR("Failed to create SenderDB: terminating")
            return -1;
        }
    }else{
        reload_from_sender_db = true;
    }

    // 打印bin bundles相关数据
    uint32_t  max_bin_bundles_per_bundle_idx = 0;
    for(uint32_t bundle_idx = 0;bundle_idx < sender_db ->get_params().bundle_idx_count();bundle_idx++){
        max_bin_bundles_per_bundle_idx = std::max(max_bin_bundles_per_bundle_idx,static_cast<uint32_t>(sender_db->get_bin_bundle_count(bundle_idx)));
    }
    APSI_LOG_INFO("SenderDB holds a total of " << sender_db->get_bin_bundle_count() << " ; bin bundles across " << sender_db->get_params().bundle_idx_count() << " bundle indices");
    APSI_LOG_INFO("The largest bundle index holds " << max_bin_bundles_per_bundle_idx << " bin bundles");

    // 存储sender_db,如果sdb_output_path参数不为空的话
    string sdb_output_path = absl::GetFlag(FLAGS_sdb_output_path);

    // 如果数据已经来自sender db文件，忽略保存sender db 的操作
    if(reload_from_sender_db && !sdb_output_path.empty()){
        APSI_LOG_WARNING("Ignore save sender db ")
    }else if(!sdb_output_path.empty() && !try_save_sender_db(sdb_output_path,sender_db,oprf_key)){
        return -1;
    }

    // 运行服务
    atomic<bool> stop = false;
    ZMQSenderDispatcher dispatch(sender_db,oprf_key);

    dispatch.run(stop,1212);
    return 0;
}

/**
 * 从SenderDB文件中读取
 * @param db_path
 * @param oprf_key
 * @return
 */
shared_ptr<SenderDB> try_load_sender_db(string db_path,OPRFKey &oprf_key){
    shared_ptr<SenderDB> result = nullptr;
    ifstream  fs(db_path,ios::binary);
    fs.exceptions(ios_base::badbit | ios_base::failbit);

    try{
        auto [data,size] = SenderDB::Load(fs);
        APSI_LOG_INFO("Loaded SenderDB (" << size <<" bytes) from " << db_path );
        // 不使用参数中给定的params文件了
        if(!absl::GetFlag(FLAGS_params_path).empty()){
            APSI_LOG_WARNING("PSI parameters were loaded with the SenderDB;ignoring given PSI parameters");
        }
        result = make_shared<SenderDB>(std::move(data));

        // 加载OPRF key
        oprf_key.load(fs);
        APSI_LOG_INFO("Loaded OPRF key (" << oprf_key_size << " bytes) from " << db_path);
    }catch(const exception &ex){
        APSI_LOG_WARNING("Failed to load SenderDB: " << ex.what());
    }
    return result;
}

// 从csv中加载db
shared_ptr<SenderDB> try_load_csv_db(string db_path,OPRFKey &oprf_key){
    unique_ptr<PSIParams> params = build_psi_param();
    if(!params){
        APSI_LOG_ERROR("Failed to get params");
        return nullptr;
    }

    unique_ptr<CSVReader::DBData> db_data;
    string db_file_path = absl::GetFlag(FLAGS_db_path);
    if(!(db_data = load_db(db_file_path))){
        APSI_LOG_ERROR("load db error");
        return nullptr;
    }
    APSI_LOG_INFO("local csv db success");


    return create_sender_db(*db_data,std::move(params),oprf_key,16,false) ;
}

unique_ptr<PSIParams> build_psi_param(){
    string params_json;
    string params_path = absl::GetFlag(FLAGS_params_path);
    try{
        fstream input_file(params_path,ios_base::in);
        if(!input_file.is_open()){
            APSI_LOG_ERROR("params file could not be open for read")
            throw runtime_error("count not open params file");
        }
        string line;
        while(getline(input_file,line)){
            params_json.append(line);
            params_json.append("\n");
        }
        APSI_LOG_INFO("param file content:" << endl << params_json) ;
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
        APSI_LOG_ERROR("read csv error" << ex.what());
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
        APSI_LOG_ERROR("No PSI parameter was given");
    }

    shared_ptr<SenderDB> sender_db;
    if(holds_alternative<CSVReader::UnlabeledData>(db_data)){
        try{
            sender_db = make_shared<SenderDB>(*psi_params,0,0,compress);
            sender_db->set_data(get<CSVReader::UnlabeledData>(db_data));
        }catch(exception &ex){
            APSI_LOG_ERROR("Failed to create SenderDb:" << ex.what());
            return nullptr;
        }
    }else if(holds_alternative<CSVReader::LabeledData>(db_data)){
        try{
            // labeled psi 先不处理
            auto &labeled_db_data  =  get<CSVReader::LabeledData>(db_data);
            // 查找label的最大长度
            size_t label_byte_count = max_element(labeled_db_data.begin(),labeled_db_data.end(),[](auto &a,auto &b){
                return a.second.size() < b.second.size();
            })->second.size();

            sender_db = make_shared<SenderDB>(*psi_params,label_byte_count,nonce_byte_count,compress);
            sender_db ->set_data(labeled_db_data);
            APSI_LOG_INFO("Created labeled SenderDB with " << sender_db->get_item_count() << " items and "
                      << label_byte_count << "-byte labels("
                      << nonce_byte_count << "-byte nonces)");
        }catch(const exception &ex){
            APSI_LOG_INFO("Failed to create SenderDb:" << ex.what());
            return nullptr;
        }
    }else{
        APSI_LOG_ERROR("UnKnown database state");
        return  nullptr;
    }
    if(compress) {
        APSI_LOG_INFO("Using in-memory compression to reduce memory footprint");
    }

    oprf_key = sender_db->strip();
    APSI_LOG_INFO("create SenderDb success");
    APSI_LOG_INFO("SenderDB packing rate: " << sender_db->get_packing_rate());
    return sender_db;
}


/**
 * 保存sender db
 * @param sdb_output_path
 * @param sender_db
 * @param oprf_key
 * @return
 */
bool try_save_sender_db(const string sdb_output_path,shared_ptr<SenderDB> sender_db,const OPRFKey oprf_key){
    if(!sender_db){
        return false;
    }
    ofstream  fs(sdb_output_path,ios::binary);
    fs.exceptions(ios_base::badbit|ios_base::failbit);
    try{
        // 保存Sender Db
        size_t size = sender_db->save(fs);
        APSI_LOG_INFO("Saved SenderDb (" <<size << " bytes) to " << sdb_output_path);

        // 保存OPRF key
        oprf_key.save(fs);

        APSI_LOG_INFO("Saved OPRF key(" << oprf_key_size << " bytes) to" << sdb_output_path )
    }catch(const exception &e){
        APSI_LOG_INFO("Failed to save SenderDb:" << e.what())
        return false;
    }

    return true;



}
