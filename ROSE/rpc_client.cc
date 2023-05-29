#include "DistSSE.client.h"
#include "DistSSE.db_generator.h"

#include "logger.h"

using DistSSE::SearchRequestMessage;

int main(int argc, char **argv) {
    DistSSE::logger::set_severity(DistSSE::logger::INFO);
    //DistSSE::logger::log(DistSSE::logger::INFO) << " client test :  "<< std::endl;
    //DistSSE::logger::log_benchmark() << "client to file" <<std::endl;
    // Instantiate the client and channel isn't authenticated
    DistSSE::Client client(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()),
                           std::string(argv[1]));

    //数据库生成
    int flag = atoi(argv[2]);
    int threads_num = atoi(argv[3]);
    if(argc == 5){
        //gen_db
        struct timeval t1, t2;
        std::string logFileName = argv[4]; //设置日志文件名
        DistSSE::logger::set_benchmark_file(logFileName); //初始化日志文件
        gettimeofday(&t1, NULL);
        vector<string> serverEDB = client.load_data_from_file("./dataset/sse_data_test");
        DistSSE::gen_db_rose(client,serverEDB,threads_num);
        gettimeofday(&t2, NULL);
        std::cout << "update done." << std::endl;
        //输出到终端
        cout<< "update time: "<< ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000. << " ms" << std::endl;
    }else if(argc == 6 && flag == 3){
        //search
        string keyword = std::string(argv[4]);
        client.search_rose(keyword);
        std::cout << "search done: " << std::endl;
    }else if(argc == 7 && flag == 2){
        //update_add
        string keyword = std::string(argv[4]);
        int N_entries = stoi(std::string(argv[5]));
        DistSSE::update_rose(client,N_entries,keyword,threads_num,1,1);
    }else if(argc == 8 && flag == 4){
        //update_del
        string keyword = std::string(argv[4]);
        int N_entries = stoi(std::string(argv[5]));
        double deleteRatio = stoi(std::string(argv[6])) * 1.0 / 100;
        DistSSE::update_rose(client,N_entries,keyword,threads_num,0,deleteRatio);
    }else if(argc == 7 && flag == 1){
        //gen_db
        string keyword = std::string(argv[4]);
        int N_entries = stoi(std::string(argv[5]));
        DistSSE::gen_db_2(client,N_entries,keyword,threads_num);
    }else if(argc == 8 && flag == 5){
        //gen_db
        string keyword = std::string(argv[4]);
        int N_entries = stoi(std::string(argv[5]));
        double repeatRatio = stoi(std::string(argv[6])) * 1.0 / 100;
        DistSSE::gen_repeat_db(client,N_entries,keyword,repeatRatio,threads_num);
    }else{
        std::cerr << "argc error" << std::endl;
        exit(-1);
    }
    return 0;
}

