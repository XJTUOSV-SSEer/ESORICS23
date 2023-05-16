#include "DistSSE.client.h"
#include "DistSSE.db_generator.h"

#include "logger.h"

using DistSSE::SearchRequestMessage;

int main(int argc, char** argv) {
    // Instantiate the client and channel isn't authenticated
	DistSSE::Client client(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()), std::string(argv[1])); //dbpath
    //DistSSE::Client client(grpc::CreateChannel("192.168.1.98:50051", grpc::InsecureChannelCredentials()), std::string(argv[1])); //dbpath
	if (argc < 7) {
        std::cerr << "argc error" << std::endl;
        exit(-1);
    }
	size_t N_entry = atoi(argv[2]); //关键字个数
    std::string w = std::string(argv[3]); //关键字
    int flag = atoi(argv[4]); //操作类型
    int threads_num = atoi(argv[5]); //线程数
	if (flag == 2) {
        //gen_db:add
        std::string logFileName = argv[6]; //设置日志文件名
        DistSSE::logger::set_benchmark_file(logFileName); //初始化日志文件
        //参数1: client; 参数2: (w,id)个数; 参数3: 关键字; 参数4: 线程数; 参数5：删除比例
        DistSSE::gen_db(client, N_entry, w, threads_num, 0.0);
        std::cout << "update done." << std::endl;
    }else if (flag == 3) {
        client.search(w);
        std::cout << "search done. " << std::endl;
    }else if(flag == 4){
        //random delete
        std::string logFileName = argv[6]; //设置日志文件名
        DistSSE::logger::set_benchmark_file(logFileName); //初始化日志文件
        //参数1: client; 参数2: (w,id)个数; 参数3: 关键字; 参数4: 线程数
        DistSSE::random_delete_db(client, N_entry, w, threads_num);
        std::cout << "delete done." << std::endl;
    }
	return 0;
}

