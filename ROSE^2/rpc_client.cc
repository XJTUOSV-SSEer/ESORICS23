#include "DistSSE.client.h"
#include "DistSSE.db_generator.h"

#include "logger.h"

using DistSSE::SearchRequestMessage;

int main(int argc, char **argv) {
    DistSSE::logger::set_severity(DistSSE::logger::INFO);
    DistSSE::Client client(grpc::CreateChannel("192.168.1.98:50051", grpc::InsecureChannelCredentials()),
                           std::string(argv[1]));

    //数据库生成
    int flag = atoi(argv[2]);
    if(argc == 5 && flag == 1){//gen_db
        //数据库生成
        int thread_num = atoi(argv[3]);
        DistSSE::gen_db_Rose_2(client,"./dataset/1-100000.txt",thread_num);
        std::cout << "gendb done." << std::endl;
    }else if(argc == 5 && flag == 3){
        //search
        std::string keyword = std::string(argv[3]);
        client.search_Rose_2(keyword);
        std::cout << "search done." << std::endl;
    }else if(argc == 6 && flag == 2){
        //update_add
        std::string keyword = std::string(argv[3]);
        std::string id = std::string(argv[4]);
        client.Update_Rose_2(keyword,id,1);
        std::cout << "update(add) done." << std::endl;
    }else if(argc == 6 && flag == 4){
        //update_del
        std::string keyword = std::string(argv[3]);
        std::string id = std::string(argv[4]);
        client.Update_Rose_2(keyword,id,0);
        std::cout << "update(del) done." << std::endl;
    }else if(argc == 7 && flag == 5){
        //genramdomdb
        std::string keyword = std::string(argv[3]);
        int N_entries = atoi(argv[4]);
        int thread_num = atoi(argv[5]);
        DistSSE::gen_db_random(client,N_entries,keyword,thread_num);
        std::cout << "gendb done." << std::endl;
    }else{
        std::cerr << "argc error" << std::endl;
        exit(-1);
    }
    return 0;
}

