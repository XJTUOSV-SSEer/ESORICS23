#include "DistSSE.client.h"
#include "DistSSE.db_generator.h"

#include "logger.h"

using DistSSE::SearchRequestMessage;

int main(int argc, char **argv) {
    DistSSE::logger::set_severity(DistSSE::logger::INFO);
    //DistSSE::logger::log(DistSSE::logger::INFO) << " client test :  "<< std::endl;
    //DistSSE::logger::log_benchmark() << "client to file" <<std::endl;
    // Instantiate the client and channel isn't authenticated
    DistSSE::Client client(grpc::CreateChannel("192.168.1.98:50051", grpc::InsecureChannelCredentials()),
                           std::string(argv[1]));

    //数据库生成
    int flag = atoi(argv[2]);
    int threads_num = atoi(argv[3]);
    if(argc == 5){//gen_db
        //数据库生成
        std::string logFileName = argv[4]; //设置日志文件名
        DistSSE::logger::set_benchmark_file(logFileName); //初始化日志文件
        vector<string> serverEDB = client.load_data_from_file("./dataset/sse_data_test");
        DistSSE::gen_db_rose(client,serverEDB,threads_num);
        std::cout << "update done." << std::endl;
    }else if(argc == 6 && flag == 3){
        //search
        string keyword = std::string(argv[4]);
        client.search_rose(keyword);
        std::cout << "search done: " << std::endl;
    }else if(argc == 7 && flag == 2){
        //update_add
        string keyword = std::string(argv[4]);
        int N_entries = stoi(std::string(argv[5]));
        DistSSE::update_rose(client,N_entries,keyword,threads_num,1);
    }else if(argc == 7 && flag == 4){
        //update_del
        string keyword = std::string(argv[4]);
        int N_entries = stoi(std::string(argv[5]));
        DistSSE::update_rose(client,N_entries,keyword,threads_num,0);
    }else{
        std::cerr << "argc error" << std::endl;
        exit(-1);
    }
    // if (argc < 5) {
    //     std::cerr << "argc error" << std::endl;
    //     exit(-1);
    // }
    // size_t N_entry = atoi(argv[2]); //关键字个数
    // std::string w = std::string(argv[3]); //关键字
    // int flag = atoi(argv[4]); //操作类型
    // int threads_num = atoi(argv[5]); //线程数
    // std::cout << "update begin..." << std::endl;
    // //if (flag == 1) DistSSE::generate_trace(&client, N_entry);
    // if (flag == 1) {
    //     //DistSSE::gen_db(client, N_entry, threads_num);
    //     std::cout << "update done." << std::endl;
    //     //client.search(w);
    //     std::cout << "search done: " << std::endl;
    // } else if (flag == 2) {
    //     //数据库生成
    //     std::string logFileName = argv[6]; //设置日志文件名
    //     DistSSE::logger::set_benchmark_file(logFileName); //初始化日志文件
    //     //参数1: client; 参数2: (w,id)个数; 参数3: 关键字; 参数4: 线程数
    //     vector<string> serverEDB = client.load_data_from_file("sse_data_test");//<<<
    //     DistSSE::gen_db_rose(client,serverEDB,threads_num);
    //     //old
    //     //DistSSE::gen_db_2(client, N_entry, w, threads_num);
    //     std::cout << "update done." << std::endl;
    // } else if (flag == 3) {
    //     client.search(w);
    //     std::cout << "search done: " << std::endl;
    // } else if (flag == 4) {
    //     std::string logFileName = argv[6];
    //     DistSSE::logger::set_benchmark_file(logFileName);
    //     client.search2(w);
    //     std::cout << "verify done: " << std::endl;
    // } else {
    //     //eval_trace(client, threads_num);
    //     std::cout << "update done." << std::endl;
    //     //client.search(w);
    //     std::cout << "search done: " << std::endl;
    // }
    return 0;
}

