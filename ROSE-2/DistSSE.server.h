/*
 * Created by Xiangfu Song on 10/21/2016.
 * Email: bintasong@gmail.com
 * 
 */
#ifndef DISTSSE_SERVER_H
#define DISTSSE_SERVER_H
#include <grpc++/grpc++.h>
#include "DistSSE.grpc.pb.h"
#include "DistSSE.Util.h"
#include "logger.h"
#include "thread_pool.h"
#include "common.h"
#include "KUPRF.h"
#include <unordered_set>
#include <math.h>
#include <OMAP/OMAP.h>
#include <OMAP/Utilities.h>
#define min(x, y) ( (x) < (y) ? (x) : (y) )
#define max(x, y) ( (x) < (y) ? (y) : (x) )

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;
using namespace std;

namespace DistSSE {
    struct Cipher
    {
        unsigned char R[16];
        unsigned char D[1 + 32 * 2 + 33];
        unsigned char C[CIPHER_SIZE];
    };
    class DistSSEServiceImpl final : public RPC::Service {
    private:
        static rocksdb::DB *ss_db;
        // static rocksdb::DB* ss_db_read;
        static rocksdb::DB *cache_db;
        int MAX_THREADS;
        static std::mutex result_mtx;
        //static std::mutex cache_write_mtx;
        int N = 16384; //2^14 
        //int N = 32768; //2^15 
        //int N = 1024; //2^10
        bytes<Key> key{0};
        OMAP* OD_del;
        OMAP* OD_tree;


    public:
        DistSSEServiceImpl(const std::string db_path, std::string cache_path, int concurrent) {
            KUPRF::init();
            OD_tree = new OMAP(OMAP_SIZE, key);
            OD_del = new OMAP(OMAP_SIZE, key);
            signal(SIGINT, abort);
            rocksdb::Options options;
            options.create_if_missing = true;
            Util::set_db_common_options(options);
            // options.use_fsync = true;
            rocksdb::Status s1 = rocksdb::DB::Open(options, db_path, &ss_db);
            // rocksdb::Status s2 = rocksdb::DB::OpenForReadOnly(options, db_path, &ss_db_read);
            // set options for merge operation
            rocksdb::Options simple_options;
            simple_options.create_if_missing = true;
            simple_options.merge_operator.reset(new rocksdb::StringAppendOperator());
            simple_options.use_fsync = true;
            rocksdb::Status s3 = rocksdb::DB::Open(simple_options, cache_path, &cache_db);
            if (!s1.ok()) {
                std::cerr << "open ssdb error:" << s1.ToString() << std::endl;
            }
            // assert(s2.ok());
            assert(s3.ok());
            MAX_THREADS = concurrent; //std::thread::hardware_concurrency();
        }
        static void abort(int signum) {
            delete ss_db;
            delete cache_db;
            logger::log(logger::INFO) << "abort: " << signum << std::endl;
            exit(signum);
        }

        static int store(rocksdb::DB *&db, const std::string l, const std::string e) {
            //logger::log(logger::INFO) << "store: "<< std::endl;
            rocksdb::Status s;
            rocksdb::WriteOptions write_option = rocksdb::WriteOptions();
            // write_option.sync = true;
            // write_option.disableWAL = false;
            {
                //std::lock_guard<std::mutex> lock(ssdb_write_mtx);
                s = db->Put(write_option, l, e);
            }
            assert(s.ok());
            if (s.ok()) return 0;
            else {
                //std::cerr << std::to_string(s) << std::endl;
                return -1;
            }
        }

        static int store_rose(rocksdb::DB *&db, const std::string L, const std::string R,const std::string D,const std::string C) {
            //logger::log(logger::INFO) << "store: "<< std::endl;
            rocksdb::Status s;
            rocksdb::WriteOptions write_option = rocksdb::WriteOptions();
            // write_option.sync = true;
            // write_option.disableWAL = false;
            {
                //std::lock_guard<std::mutex> lock(ssdb_write_mtx);
                s = db->Put(write_option, "R"+ L, R);
                // print_hex((unsigned char*)L.c_str(), L.length());
                // print_hex((unsigned char*)("R" + L).c_str(), ("R"+ L).length());
                s = db->Put(write_option, "D"+ L, D);
                s = db->Put(write_option, "C"+ L, C);
            }
            assert(s.ok());
            if (s.ok()) return 0;
            else {
                //std::cerr << std::to_string(s) << std::endl;
                return -1;
            }
        }
        static std::string get(rocksdb::DB *&db, const std::string l) {
            std::string tmp;
            rocksdb::Status s;
            //redo:
            {
                //std::lock_guard<std::mutex> lock(ssdb_write_mtx);
                s = db->Get(rocksdb::ReadOptions(), l, &tmp);
            }
            //if (!s.ok())	std::cerr << "in get() " << s.ToString()<<", tmp: "<< tmp << std::endl;
            return tmp;
        }

        static size_t getDBSize(rocksdb::DB *&db){
            rocksdb::Iterator *it = db->NewIterator(rocksdb::ReadOptions());
            size_t counter = 0;
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                counter++;
            }
            return counter;
        }

        static bool get(rocksdb::DB *&db, const std::string l, std::string &e) {
            //int num_keys;
            //db -> GetAggregatedIntProperty("rocksdb.estimate-num-keys", &num_keys);
            //logger::log(logger::ERROR) << std::to_string(num_keys)+" keys in database" << std::endl;

            rocksdb::Status s;
            {
                //std::lock_guard<std::mutex> lock(ssdb_write_mtx);
                s = db->Get(rocksdb::ReadOptions(), l, &e);
            }
            return s.ok();
        }


        static int delete_entry(rocksdb::DB *&db, const std::string l) {
            int status = -1;
            try {
                rocksdb::WriteOptions write_option = rocksdb::WriteOptions();
                //write_option.sync = true;
                rocksdb::Status s;
                s = db->Delete(write_option, l);
                if (s.ok()) status = 0;
            } catch (std::exception &e) {
                std::cerr << "in delete_entry() " << e.what() << std::endl;
                exit(1);
            }
            return status;
        }

        static void search_log(std::string kw, double search_time, int result_size) {
            // std::ofstream out( "search.slog", std::ios::out|std::ios::app);
            CryptoPP::byte k_s[17] = "0123456789abcdef";
            CryptoPP::byte iv_s[17] = "0123456789abcdef";

            std::string keyword = Util::dec_token(k_s, AES128_KEY_LEN, iv_s, kw);

            std::string word = keyword == "" ? "cached" : keyword;
            double per_entry_time = result_size == 0 ? search_time : (search_time / result_size);
            std::cout << Util::str2hex(word) + "\t" + std::to_string(result_size) + "\t Total time:" + std::to_string(search_time) + " ms\t Average time:" +
                         std::to_string(per_entry_time)+" ms" << std::endl;

            DistSSE::logger::log_benchmark() << word + "\t" + std::to_string(result_size) + "\t Total time:" + std::to_string(search_time) + " ms\t Average time:" +
                                                std::to_string(per_entry_time)+" ms" << std::endl;
        }

        static void parse(std::string str, std::string &op, std::string &ind) {
            op = str.substr(0, 1);
            ind = str.substr(1, 7);
        }

        Status search_Rose_2(ServerContext *context, const SearchRequestMessage_Rose_2 *request, ServerWriter <SearchReply_Rose_2> *writer) {
            //logger::log(logger::INFO) << "search_Rose_2(ServerContext *context, const SearchRequestMessage_Rose_2 *request, ServerWriter <SearchReply_Rose_2> *writer)"<< std::endl;
            string K_u = request->k_u();
            string delta_k = request->delta_k();
            string tk = request->tk();
            int sn = request->sn();
            int cnt_d = request->cnt_d();
            int cnt_i = request->cnt_i();
            queue<pair<int,int>> T;
            vector<pair<string,string>> M;
            vector<string> R;
            T.push(make_pair(sn,cnt_d));
            KUPRF kuprf;
            unsigned char buf_L[256];
            string L;
            string C;
            string D;
            //int i = 0;
            while(T.size() != 0){
                //i++;
                int n = T.front().first;
                //cout<<"n:"<<n<<endl;
                int num = T.front().second;
                //cout<<"num:"<<num<<endl;
                T.pop();
                kuprf.Eval(buf_L, (unsigned char*)K_u.c_str(), Util::H1(tk+std::to_string(n)+std::to_string(num)));
                L.assign((char*)buf_L,33);
                if(get(ss_db,L,C) == true && n > N){
                    string I_n = Util::Xor(Util::H1(tk + to_string(n) + to_string(num)),C);
                    vector<int> I_n_v;
                    int index1 = 0;
                    int index2 = 0;
                    int lNode = 0;
                    int lNum = 0;
                    int rNode = 0;
                    int rNum = 0;
                    for(index2=0;index2<I_n.length();index2++){
                        if(I_n[index2] == '*'){
                            I_n_v.push_back(atoi(I_n.substr(index1,index2 - index1).c_str()));
                            index1 = index2 + 1;
                        }
                        if(I_n[index2] == '#'){
                            break;
                        }
                    }
                    lNode = I_n_v[0];
                    lNum = I_n_v[1];
                    rNode = I_n_v[2];
                    rNum = I_n_v[3];
                    //cout<<"Node:"<<n<<" lNode:"<< lNode << " lNum:"<< lNum << " rNode:"<< rNode << " rNum:"<< rNum<<endl; 
                    T.push(make_pair(lNode,lNum));
                    int rNode_h = get_h(rNode);
                    int rNode_lln = 2 * N + pow(2,rNode_h-1)*(rNode-1) - pow(2,rNode_h)*N + 1; 
                    if(rNode_lln <= cnt_i){
                        T.push(make_pair(rNode,rNum));
                    }
                }else if(n<=N){
                    string hash = Util::H1(tk + to_string(n) + "1");
                    kuprf.Eval(buf_L, (unsigned char*)K_u.c_str(), hash);
                    L.assign((char*) buf_L, 33);
                    M.push_back(make_pair(L,hash));
                    get(ss_db, L, C);
                    R.push_back(C);
                }else{
                    int h = get_h(n);
                    int lln = 2*N + pow(2,h-1)*(n-1) - pow(2,h)*N + 1;
                    int rln = lln + pow(2,h-1) - 1;
                    if(cnt_i < rln){
                        rln = cnt_i;
                    }
                    for(int k = rln;k>=lln;k--){
                        string hash = Util::H1(tk + to_string(k) + "1");
                        if(k == rln){
                            kuprf.Eval(buf_L, (unsigned char*)K_u.c_str(), hash);
                            L.assign((char*) buf_L, 33);
                            M.push_back(make_pair(L,hash));
                            get(ss_db,"d" + L, D);
                            get(ss_db, L, C);
                        }else{
                            M.push_back(make_pair(L,hash));
                            get(ss_db,"d" + L, D);
                            get(ss_db, L, C);
                        }
                        // cout<<k<<"-L:"<<Util::str2hex(L)<<endl;
                        // cout<<k<<"-D:"<<Util::str2hex(D)<<endl;
                        L = Util::Xor(D,hash + "0");
                        // cout<<k<<"-C:"<<Util::str2hex(C)<<endl;
                        R.push_back(C);
                    }
                }
            }
            SearchReply_Rose_2 reply;
            for (std::vector<std::string>::iterator i = R.begin(); i != R.end(); i++) {
                reply.set_c(*i);
                writer->Write(reply);
            }
            //ReUpdate(M,delta_k,kuprf);
            return Status::OK;
        }

        void ReUpdate(vector<pair<string,string>> M,string delta_k,KUPRF kuprf){
            unsigned char buf_L[256];
            for(int i=0;i<M.size();i++){
                string L = M[i].first;
                string U = M[i].second;
                string C;
                string D;
                get(ss_db,L,C);
                get(ss_db,"d" + L,D);
                kuprf.update(buf_L, (unsigned char*)delta_k.c_str(),(unsigned char*)L.c_str());
                delete_entry(ss_db,L);
                delete_entry(ss_db,"d" + L);
                L.assign((char*)buf_L,33);
                kuprf.update(buf_L, (unsigned char*)delta_k.c_str(),(unsigned char*)(Util::Xor(D,U + "0")).c_str());
                string temp;
                temp.assign((char*)buf_L,33);
                D = Util::Xor(temp, U + "0");
                store(ss_db, "d" + L, D);
                store(ss_db, L, C);
            }
        }

        int get_h(int n){
            int h;
            h = ceil(1+log(N)/log(2)-log(2*N-n)/log(2));
            return h;
        }

        // server RPC
        // search() 实现搜索操作
        Status search_rose(ServerContext *context, const SearchRequestMessage *request, ServerWriter <SearchReply> *writer) {
            logger::log(logger::INFO) << "server: search_rose(ServerContext *context, const SearchRequestMessage *request, ServerWriter <SearchReply> *writer)"<< std::endl;
            string tpd_L, tpd_T, cip_L, cip_R, cip_D, cip_C;
            vector<std::string> result;
            tpd_L = request->tpd_l();
            tpd_T = request->tpd_t();
            cip_L = request->l();
            cip_R = request->r();
            cip_D = request->d();
            //print_hex((unsigned char*)cip_D.c_str(),cip_D.length());
            cip_C = request->c();
            //print_hex((unsigned char*)cip_C.c_str(),cip_C.length());
            SearchReply reply;
            Cipher *cip = new Cipher;
            unsigned char buf1[256], buf2[256], buf3[256], buf_Dt[256], buf_Deltat[256];
            OpType opt;
            vector<string> D;
            bool is_delta_null = true;
            string s_Lt, s_L1t, s_L1, s_T1, s_T1t, s_tmp;
            set<string> L_cache;
            KUPRF kuprf;
            memcpy(cip->R, cip_R.c_str(), 16);
            memcpy(cip->D, cip_D.c_str(), 1 + 33 + 32 * 2);
            //memcpy(cip->C, cip_D.c_str(), CIPHER_SIZE);
            memcpy(cip->C, cip_C.c_str(), CIPHER_SIZE);


            
            //_store[cip_L] = cip;
            store_rose(ss_db, cip_L, (char*)cip->R, (char*)cip->D, (char*)cip->C);

            s_Lt = cip_L;
            memcpy(buf_Dt, cip_D.c_str(), 1 + 33 + 32 * 2);
            opt = op_srh;
            is_delta_null = true;

            s_L1 = s_L1t = tpd_L;
            s_T1 = s_T1t = tpd_T;

            while (true)
            {   
                L_cache.emplace(s_L1);
                //cip = _store[s_L1];
                cip = new Cipher;
                memcpy(cip->R, get(ss_db,"R"+s_L1).c_str(), 16);
                memcpy(cip->D, get(ss_db,"D"+s_L1).c_str(), 1 + 33 + 32 * 2);
                memcpy(cip->C, get(ss_db,"C"+s_L1).c_str(), CIPHER_SIZE);

                Hash_H(buf2, 1 + 32 * 2 + 33, (const unsigned char *) s_T1.c_str(), cip->R);
                Xor(1 + 33 + 32 * 2, cip->D, buf2, buf3);
                cout<<(int)buf3[0]<<endl;
                if (buf3[0] == 0xf0) // del
                {
                    L_cache.erase(s_L1);

                    //_store.erase(s_L1);
                    delete_entry(ss_db,"R"+s_L1);
                    delete_entry(ss_db,"D"+s_L1);
                    delete_entry(ss_db,"C"+s_L1);

                    delete cip;

                    s_tmp.assign((const char *) buf3 + 1, 33);
                    D.emplace_back(s_tmp);

                    Xor(32, (const unsigned char *) s_L1t.c_str(), (const unsigned char *) buf3 + 1 + 33, buf2);
                    Xor(32, (const unsigned char *) s_T1t.c_str(), (const unsigned char *) buf3 + 1 + 33 + 32, buf2 + 32);
                    Xor(64, buf_Dt + 1 + 33, buf2, buf_Dt + 1 + 33);

                    //cip = _store[s_Lt];
                    cip = new Cipher;
                    memcpy(cip->R, get(ss_db,"R"+s_Lt).c_str(), 16);
                    memcpy(cip->D, get(ss_db,"D"+s_Lt).c_str(), 1 + 33 + 32 * 2);
                    memcpy(cip->C, get(ss_db,"C"+s_Lt).c_str(), CIPHER_SIZE);
                    memcpy(cip->D, buf_Dt, 1 + 32 * 2 + 33);

                    //update!!!
                    store(ss_db,"D"+s_Lt, (char*)cip->D);

                    s_L1t.assign((const char *) buf3 + 1 + 33, 32);
                    s_T1t.assign((const char *) buf3 + 1 + 33 + 32, 32);
                }
                else if (buf3[0] == 0x0f) //add
                {
                    for (auto itr = D.rbegin(); itr != D.rend(); itr++)
                    {
                        Hash_G(buf1, (const unsigned char *) itr->c_str(), cip->R);
                        if (memcmp(buf1, s_L1.c_str(), 32) == 0)
                        {
                            L_cache.erase(s_L1);
                            //_store.erase(s_L1);
                            delete_entry(ss_db,"R"+s_L1);
                            delete_entry(ss_db,"D"+s_L1);
                            delete_entry(ss_db,"C"+s_L1);

                            delete cip;

                            Xor(32, (const unsigned char *) s_L1t.c_str(), (const unsigned char *) buf3 + 1 + 33, buf2);
                            Xor(32, (const unsigned char *) s_T1t.c_str(), (const unsigned char *) buf3 + 1 + 33 + 32,
                                buf2 + 32);
                            Xor(64, buf_Dt + 1 + 33, buf2, buf_Dt + 1 + 33);

                            //cip = _store[s_Lt];
                            cip = new Cipher;
                            memcpy(cip->R, get(ss_db,"R"+s_Lt).c_str(), 16);
                            memcpy(cip->D, get(ss_db,"D"+s_Lt).c_str(), 1 + 33 + 32 * 2);
                            memcpy(cip->C, get(ss_db,"C"+s_Lt).c_str(), CIPHER_SIZE);

                            memcpy(cip->D, buf_Dt, 1 + 32 * 2 + 33);
                            //update!!
                            store(ss_db,"D"+s_Lt, (char*)cip->D);

                            s_L1t.assign((const char *) buf3 + 1 + 33, 32);
                            s_T1t.assign((const char *) buf3 + 1 + 33 + 32, 32);
                            cip = nullptr;
                            break;
                        }
                    }
                    if (cip != nullptr)
                    {
                        s_Lt = s_L1;
                        memcpy(buf_Dt, cip->D, 1 + 32 * 2 + 33);
                        s_L1t.assign((const char *) buf3 + 1 + 33, 32);
                        s_T1t.assign((const char *) buf3 + 1 + 33 + 32, 32);
                        opt = op_add;
                        s_tmp.assign((const char *) cip->C, CIPHER_SIZE);
                        result.emplace_back(s_tmp);
                    }
                }
                else
                {
                    if (opt == op_srh && (!is_delta_null))
                    {
                        L_cache.erase(s_L1);
                        //_store.erase(s_L1);
                        delete_entry(ss_db,"R"+s_L1);
                        delete_entry(ss_db,"D"+s_L1);
                        delete_entry(ss_db,"C"+s_L1);
                        delete cip;

                        kuprf.mul(buf1, buf_Deltat, buf3 + 1);

                        Xor(32, buf_Deltat, buf1, buf_Deltat);
                        Xor(32, buf_Dt + 1, buf_Deltat, buf_Dt + 1);

                        Xor(32, (const unsigned char *) s_L1t.c_str(), buf3 + 1 + 33, buf2);
                        Xor(32, (const unsigned char *) s_T1t.c_str(), buf3 + 1 + 33 + 32, buf2 + 32);
                        Xor(64, buf_Dt + 1 + 33, buf2, buf_Dt + 1 + 33);

                        //cip = _store[s_Lt];
                        cip = new Cipher;
                        memcpy(cip->R, get(ss_db,"R"+s_Lt).c_str(), 16);
                        memcpy(cip->D, get(ss_db,"D"+s_Lt).c_str(), 1 + 33 + 32 * 2);
                        memcpy(cip->C, get(ss_db,"C"+s_Lt).c_str(), CIPHER_SIZE);

                        memcpy(cip->D, buf_Dt, 1 + 32 * 2 + 33);
                        //update!!
                        store(ss_db,"D"+s_Lt, (char*)cip->D);

                        memcpy(buf_Deltat, buf1, 32);
                        s_L1t.assign((const char *) buf3 + 1 + 33, 32);
                        s_T1t.assign((const char *) buf3 + 1 + 33 + 32, 32);
                    }
                    else
                    {
                        s_Lt = s_L1;
                        memcpy(buf_Dt, cip->D, 1 + 32 * 2 + 33);
                        s_L1t.assign((const char *) buf3 + 1 + 33, 32);
                        s_T1t.assign((const char *) buf3 + 1 + 33 + 32, 32);
                        opt = op_srh;
                        memcpy(buf_Deltat, buf3 + 1, 32);
                        is_delta_null = false;
                    }
                    for (auto itr = D.begin(); itr != D.end(); itr++)
                    {
                        kuprf.update(buf1, buf3 + 1, (const unsigned char *) itr->c_str());//
                        itr->assign((const char *) buf1, 33);
                    }
                }
                memset(buf2, 0, 64);
                if (memcmp(buf2, buf3 + 1 + 33, 64) == 0)
                    break;
                s_L1.assign((const char *) buf3 + 1 + 33, 32);
                s_T1.assign((const char *) buf3 + 1 + 33 + 32, 32);
            }
            if (D.empty())
            {
                for (auto itr = L_cache.begin(); itr != L_cache.end(); itr++)
                {
                    //Cipher *cip = _store[*itr];
                    //delete cip;
                    delete_entry(ss_db,"R"+*itr);
                    delete_entry(ss_db,"D"+*itr);
                    delete_entry(ss_db,"C"+*itr);
                    //_store.erase(*itr);
                }
            }

            for (std::vector<std::string>::iterator i = result.begin(); i != result.end(); i++) {
                reply.set_ind(*i);
                writer->Write(reply);
            }
            return Status::OK;
        }

        /*batch_update()实现批量更新操作*/
        Status batch_update_rose(ServerContext *context, ServerReader <UpdateRequestMessage> *reader, ExecuteStatus *response) {
            std::string L,R,D,C;
            //输出到终端
            logger::log(logger::INFO) << "server batch_update_rose(ServerContext *context, ServerReader<UpdateRequestMessage> *reader, ExecuteStatus *response)"<< std::endl;
            UpdateRequestMessage request;
            while (reader->Read(&request)) {
                L = request.l();
                R = request.r();
                D = request.d();
                C = request.c();
                //std::cout<<"in update(), counter:  "<<request.counter()<<std::endl;
                store_rose(ss_db, L, R, D, C);
                //  assert(status == 0);
            }
            response->set_status(true);
            return Status::OK;
        }
        Status batch_update_Rose_2(ServerContext *context, ServerReader <UpdateRequestMessage_Rose_2> *reader, ExecuteStatus *response) {
            std::string L;
            std::string C;
            std::string D;
            logger::log(logger::INFO) << "batch_update_Rose_2(ServerContext *context, ServerReader <UpdateRequestMessage_Rose_2> *reader, ExecuteStatus *response)"<< std::endl;
            UpdateRequestMessage_Rose_2 request;
            while(reader->Read(&request)){
                L = request.l();
                C = request.c();
                D = request.d();
                store(ss_db, L, C);
                store(ss_db, "d"+ L, D);
            }
            response->set_status(true);
            return Status::OK;
        }

        // update()实现单次更新操作
        Status update_Rose_2(ServerContext *context, const UpdateRequestMessage_Rose_2 *request, ExecuteStatus *response) {
            std::string l = request->l();
            std::string c = request->c();
            std::string d = request->d();
            logger::log(logger::INFO) << "server update_Rose_2(ServerContext *context, const UpdateRequestMessage_Rose_2 *request, ExecuteStatus *response): "<< std::endl;
            int status1 = store(ss_db, l, c);
            int status2 = store(ss_db, "d" + l, d);
            //assert(status == 0);
            if (status1 != 0 || status2 != 0) {
                response->set_status(false);
                return Status::CANCELLED;
            }
            response->set_status(true);
            return Status::OK;
        }

        Status OMAPInsert(ServerContext *context, const OMAPInsertMessage *request, ExecuteStatus *response) {
            logger::log(logger::INFO) << "OMAPInsert(ServerContext *context, const OMAPInsertMessage *request, ExecuteStatus *response)"<< std::endl;
            std::string w = request->w();
            std::string idorn = request->idorn();
            std::string value = request->value();
            Bid key_OD = Utilities::getBid(w + "#" + idorn);
            if(request->flag() == 0){
                //OD_del
                OD_del->insert(key_OD,value);
            }else{
                //OD_tree
                OD_tree->insert(key_OD,value);
            }

            response->set_status(true);
            return Status::OK;
        }

        Status OMAPFind(ServerContext *context, const OMAPFindMessage *request, OMAPFindReply* reply){
            //logger::log(logger::INFO) << "OMAPFind(ServerContext *context, const OMAPFindMessage *request, OMAPFindReply* reply)"<< std::endl;
            std::string w = request->w();
            std::string idorn = request->idorn();
            //cout<<"收到key:("<<w + "," + idorn + ")" <<endl;
            Bid key_OD = Utilities::getBid(w + "#" + idorn);
            std::string value;
            if(request->flag() == 0){
                //OD_del
                value = OD_del->find(key_OD);
            }else{
                //OD_tree
                value = OD_tree->find(key_OD);
            }
            //cout<<"得到value:"<<value<<endl;
            reply->set_value(value);
            return Status::OK;
        }

        Status batchOMAPFind(ServerContext *context, const batchOMAPFindMessage *request, batchOMAPFindReply* reply){
            //logger::log(logger::INFO) <<"batchOMAPFind(ServerContext *context, const batchOMAPFindMessage *request, batchOMAPFindReply* reply)"<< std::endl;
            std::string w = request->w();
            int flag = request->flag();
            std::string value;
            for(int i=0;i<request->idorn_size();i++){
                std::string idorn = request->idorn(i);
                Bid key_OD = Utilities::getBid(w + "#" + idorn);
                //cout<<"收到key:("<<w + "," + idorn + ")" <<endl;
                if(flag == 0){
                    value = OD_del->find(key_OD);
                }else{
                    value = OD_tree->find(key_OD);
                }
                //cout<<"准备发送value:"<<value<<endl;
                reply->add_value(value);
            }
            return Status::OK;
        }
        
        Status batchOMAPInsert(ServerContext *context, ServerReader <OMAPInsertMessage>* reader, ExecuteStatus* reply){
            //logger::log(logger::INFO) <<"batchOMAPInsert(ServerContext *context, ServerReader <OMAPInsertMessage> reader, ExecuteStatus* reply)"<< std::endl;
            OMAPInsertMessage request;
            while(reader->Read(&request)){
                Bid key_OD = Utilities::getBid(request.w() + "#" + request.idorn());
                //cout<<"收到key:("<<request.w() + "," + request.idorn() + ")" <<endl;
                //cout<<"收到value:"<<request.value()<<endl;
                if(request.flag() == 0){
                    OD_del->insert(key_OD,request.value());
                }else{
                    OD_tree->insert(key_OD,request.value());
                }
                //cout<<"插入value:"<<request.value()<<endl;
            }
            reply->set_status(true);
            return Status::OK;
        }
    };

}// namespace DistSSE

// static member must declare out of main function !!!
rocksdb::DB *DistSSE::DistSSEServiceImpl::ss_db;
// rocksdb::DB* DistSSE::DistSSEServiceImpl::ss_db_read;
rocksdb::DB *DistSSE::DistSSEServiceImpl::cache_db;

std::mutex DistSSE::DistSSEServiceImpl::result_mtx;
//std::mutex DistSSE::DistSSEServiceImpl::cache_write_mtx;

void RunServer(std::string db_path, std::string cache_path, int concurrent, std::string logFilePath) {
    DistSSE::logger::set_benchmark_file(logFilePath); //设置日志文件
    std::string server_address("0.0.0.0:50051"); //初始化服务器地址
    DistSSE::DistSSEServiceImpl service(db_path, cache_path, concurrent); //初始化服务
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials()); //添加端口号
    builder.RegisterService(&service); //服务注册
    std::unique_ptr <Server> server(builder.BuildAndStart());
    DistSSE::logger::log(DistSSE::logger::INFO) << "Server listening on " << server_address << std::endl;
    server->Wait();
}

#endif // DISTSSE_SERVER_H
