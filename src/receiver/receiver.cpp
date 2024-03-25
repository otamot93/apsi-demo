//
// Created by mac on 2024/2/29.
//

// std
#include <iostream>
#include <fstream>

// absl
#include <absl/log/log.h>

// apsi
#include <apsi/network/zmq/zmq_channel.h>
#include <apsi/receiver.h>
#include <apsi/log.h>

// common
#include "common/csv_reader.h"

using namespace std;
using namespace apsi;
using namespace apsi::receiver;

// apsi
using namespace apsi::network;

// load db from csv
pair<unique_ptr<CSVReader::DBData>,vector<string>> load_db(const string &db_file);

/**
 * output intersection results;
 * @param orig_items
 * @param items
 * @param intersection
 * @param out_file
 */
void print_intersection_result(
        const vector<string> & orig_items,const vector<Item> &items,
        const vector<MatchRecord> &intersection,
        const string &out_file
        );

/**
 * print transmiited data size
 * @param channel
 */
void print_transmitted_data(Channel &channel);

int main(){

    // connect network
    string conn_address = "tcp://127.0.0.1:1212";
//    std::cout << "hello world" << std::endl;
    apsi::Log::SetLogLevel(apsi::Log::Level::all);
    APSI_LOG_INFO("Connection to " << conn_address);

    ZMQReceiverChannel channel;
    channel.connect(conn_address);
    if(channel.is_connected()){
        LOG(INFO) << "Successfully connect to " << conn_address;
    }else{
        LOG(ERROR) << "Failed connect to " << conn_address;
        return -1;
    }

    // receive parameter
    unique_ptr<PSIParams> params;
    try{
        LOG(INFO) << "Sending parameter request";
        params = make_unique<PSIParams>(Receiver::RequestParams(channel));
        LOG(INFO) << "Received valid parameters";
    }catch(exception &ex){
        LOG(ERROR) << "Failed to receive valid parameters:" << ex.what();
        return -1;
    }

    // load data
    string db_file = "/Users/mac/Documents/workspace/cpp/apsi-test/data/query.csv";
    auto [query_data,orig_items] = load_db(db_file);
    if(!query_data || !holds_alternative<CSVReader::UnlabeledData>(*query_data)){
        LOG(ERROR) << "Failed to read query file:terminating";
        return -1;
    }

    // use oprf function to items
    auto &items = get<CSVReader::UnlabeledData>(*query_data);
    vector<Item> items_vec(items.begin(),items.end());

    vector<HashedItem> oprf_items;
    vector<LabelKey> label_keys;
    try{
        LOG(INFO) << "Sending OPRF request for " << items_vec.size() << " items";
        tie(oprf_items,label_keys) = Receiver::RequestOPRF(items_vec,channel);
        LOG(INFO) << "Received OPRF response for " << items_vec.size() << " items";
    }catch(exception &ex){
        LOG(ERROR) << "OPRF request failed:" << ex.what();
        return -1;
    }

    // query
    vector<MatchRecord> query_result;
    Receiver receiver(*params);
    try{
        LOG(INFO) << "Sending APSI query";
        query_result  = receiver.request_query(oprf_items,label_keys,channel);
        LOG(INFO) << "Receive APSI query response";
    }catch(exception &ex){
        LOG(ERROR) << "Failed sending  APSI query:" << ex.what();
    }

    // output intersection result
    print_intersection_result(orig_items,items_vec,query_result,"/Users/mac/Documents/workspace/cpp/apsi-test/data/result.csv");

    // output transmitted data size
    print_transmitted_data(channel);


    return 0;
}

pair<unique_ptr<CSVReader::DBData>,vector<string>> load_db(const string &db_file){
    CSVReader::DBData db_data;
    vector<string> orig_items;

    try{
        CSVReader reader(db_file);
        tie(db_data,orig_items) = reader.read();
    }catch(exception &ex){
        LOG(ERROR) << "Count not open or read file " << db_file << ":" << ex.what();
        return {nullptr,orig_items};
    }
    return { make_unique<CSVReader::DBData>(std::move(db_data)),std::move(orig_items)};
}


void print_intersection_result(
        const vector<string> & orig_items,const vector<Item> &items,
        const vector<MatchRecord> &intersection,
        const string &out_file
){
    if(orig_items.size() != items.size()){
        throw invalid_argument("orig_items must have same size as items");
    }
    stringstream csv_output;
    for(size_t i = 0;i< orig_items.size();i++){
        if(intersection[i].found){
            csv_output << orig_items[i] << endl;
        }
    }
    if(! out_file.empty()){
        ofstream ofs(out_file);
        ofs << csv_output.str();
        LOG(INFO) << "Wrote output to " << out_file;

    }
}

void print_transmitted_data(Channel &channel){
    auto nice_byte_count = [](uint64_t bytes) -> string{
        stringstream ss;
        if(bytes >= 10 * 1024){
            ss << bytes / 1024 << " KB";
        }else{
            ss << bytes << " B";
        }
        return ss.str();
    };

    LOG(INFO) << "Communication R->S: " << nice_byte_count(channel.bytes_sent());
    LOG(INFO) << "Communication S->R:" << nice_byte_count(channel.bytes_received());
    LOG(INFO) << "Communication total:" << nice_byte_count(channel.bytes_sent()+channel.bytes_received());
}