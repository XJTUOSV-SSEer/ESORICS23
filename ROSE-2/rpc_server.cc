#include "DistSSE.server.h"

int main(int argc, char *argv[]) {
    DistSSE::logger::set_severity(DistSSE::logger::INFO);
    if (argc < 5) {
        std::cerr << "argc error" << std::endl;
        exit(-1);
    }
    //参数1: roksdb数据库路径; 参数2: cache路径; 参数3: roksdb数据库路径; 参数4: 日志名;    
    RunServer(std::string(argv[1]), std::string(argv[2]), atoi(argv[3]), std::string(argv[4]));
}