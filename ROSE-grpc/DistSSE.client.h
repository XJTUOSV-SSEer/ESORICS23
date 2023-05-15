/* 
 * Created by Xiangfu Song on 10/21/2016.
 * Email: bintasong@gmail.com
 * 
 */
#ifndef DISTSSE_CLIENT_H
#define DISTSSE_CLIENT_H

extern "C"
{
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
}
#include <iostream>
#include <grpc++/grpc++.h>
#include <experimental/filesystem>
#include "DistSSE.grpc.pb.h"
#include "DistSSE.Util.h"
#include "common.h"
#include "logger.h"
#include "KUPRF.h"


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderInterface;
using grpc::ClientWriterInterface;
using grpc::ClientAsyncResponseReaderInterface;
using grpc::Status;
using namespace CryptoPP;
using namespace std;

// 用来生成 kw
byte k_s[17] = "0123456789abcdef";
byte iv_s[17] = "0123456789abcdef";

/*
// 用来生成加密 label
byte k_l[17] = "abcdef1234567890";
byte iv_l[17] = "0123456789abcdef";

// 用来生成搜索 token
byte k_st[17] = "123456789abcdef0";
byte iv_st[17] = "0abcdef123456789";
*/

extern int max_keyword_length;
extern int max_nodes_number;

namespace DistSSE {

    class Client {
    private:
        std::unique_ptr <RPC::Stub> stub_;
        rocksdb::DB *cs_db;
        std::mutex sc_mtx;
        std::mutex uc_mtx;
        std::map <std::string, std::string> sc_mapper; // 哈希表存储search counter
        std::map <std::string, size_t> uc_mapper; // 哈希表存储update counter

        //ROSE
        unsigned char Kse[16];
        std::map<std::string,int> LastId;
        std::map<std::string, std::string> LastK, LastS, LastR;
        std::map<std::string, OpType> LastOp;
        int Enc_id(std::string &C_out, const int id)
        {
            AES_KEY aes_key;
            unsigned char iv[16], iv1[16];
            unsigned char cipher_out[CIPHER_SIZE];
            unsigned char plain[CIPHER_SIZE - 16];

            RAND_bytes(iv, 16);
            memcpy(iv1, iv, 16);

            memset(plain, 0, CIPHER_SIZE - 16);
            strncpy((char *) plain, (char*)&id, 4);

            AES_set_encrypt_key(this->Kse, 128, &aes_key);
            AES_cbc_encrypt(plain, cipher_out, CIPHER_SIZE - 16, &aes_key, iv, AES_ENCRYPT);

            memcpy(cipher_out + CIPHER_SIZE - 16, iv1, 16);
            C_out.assign((char *) cipher_out, CIPHER_SIZE);
            return 0;
        }
        int Dec_id(int &id_out, const string &C_in)
        {
            AES_KEY aes_key;
            unsigned char iv[16];
            unsigned char plain_out[CIPHER_SIZE];

            memset(plain_out, 0, CIPHER_SIZE);
            memcpy(iv, C_in.c_str() + CIPHER_SIZE - 16, 16);

            AES_set_decrypt_key(this->Kse, 128, &aes_key);
            print_hex(this->Kse,16);

            AES_cbc_encrypt((const unsigned char *) C_in.c_str(), plain_out, CIPHER_SIZE - 16, &aes_key, iv, AES_DECRYPT);

            memcpy(&id_out, plain_out, 4);

            return 0;
        }


    public:
        int get_Number(){
            return LastId.size();
        }

        
        Client(std::shared_ptr <Channel> channel, std::string db_path) : stub_(RPC::NewStub(channel)) {
            RAND_bytes(this->Kse, 16);
            rocksdb::Options options;
            // Util::set_db_common_options(options);
            // set options for merge operation
            rocksdb::Options simple_options;
            simple_options.create_if_missing = true;
            simple_options.merge_operator.reset(new rocksdb::StringAppendOperator());
            simple_options.use_fsync = true;
            rocksdb::Status status = rocksdb::DB::Open(simple_options, db_path, &cs_db);
            // load all sc, uc to memory
            rocksdb::Iterator *it = cs_db->NewIterator(rocksdb::ReadOptions());
            std::string key;
            std::string value;
            size_t counter = 0;
            //初始化KUPRF，不然会段错误
            KUPRF::init();
            std::cout << "--------The content of Rocksdb------------" << std::endl;
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                //ROSE
                /**
                 * rocksdb中的key
                 * LastId：以i开头
                 * LastK：以k开头
                 * LastS：以s开头
                 * LastR：以r开头
                 * LastOp：以o开头
                */

                key = it->key().ToString();
                value = it->value().ToString();
                if(key[0] == 'i'){
                    LastId[key.substr(1, key.length() - 1)] = std::stoi(value);
                }else if(key[0] == 'k'){
                    LastK[key.substr(1, key.length() - 1)] = value;
                }else if(key[0] == 's'){
                    LastS[key.substr(1, key.length() - 1)] = value;
                }else if(key[0] == 'r'){
                    LastR[key.substr(1, key.length() - 1)] = value;
                }else if(key[0] == 'o'){
                    if(std::stoi(value) == 0){
                        LastOp[key.substr(1, key.length() - 1)] = op_del;
                    }else if(std::stoi(value) == 1){
                        LastOp[key.substr(1, key.length() - 1)] = op_add;
                    }else if(std::stoi(value) == 2){
                        LastOp[key.substr(1, key.length() - 1)] = op_srh;
                    }
                }else if(key == "KSE"){
                    memcpy((char*)Kse,value.c_str(),16);
                }
                counter++;
            }
            std::cout << "------------------------------------------" << std::endl;
            // assert( it->status().ok() ); // Check for any errors found during the scan
            /*if(it->status().ok() == 0 ) */{
                std::cout << "client db status: " << it->status().ToString() << std::endl;
            }
            delete it;
            //std::cout << "Just remind, previous keyword counter: " << counter / 2 << std::endl;
            std::cout << "Just remind, previous keyword counter: " << counter / 5 << std::endl;
        }

        ~Client() {
            //ROSE save to rocksdb
            size_t keyword_counter = 0;
            std::map<std::string, int>::iterator it;
            for (it = LastId.begin(); it != LastId.end(); ++it) {
                store("i" + it->first, std::to_string(it->second));
                keyword_counter++;
            }

            std::map<std::string, std::string>::iterator it2;
            for (it2 = LastK.begin(); it2 != LastK.end(); ++it2) {
                store("k" + it2->first, it2->second);
            }

            std::map<std::string, std::string>::iterator it3;
            for (it3 = LastS.begin(); it3 != LastS.end(); ++it3) {
                store("s" + it3->first, it3->second);
            }

            std::map<std::string, std::string>::iterator it4;
            for (it4 = LastR.begin(); it4 != LastR.end(); ++it4) {
                store("r" + it4->first, it4->second);
            }

            std::map<std::string, OpType>::iterator it5;
            for (it5 = LastOp.begin(); it5 != LastOp.end(); ++it5) {
                if(it5->second == op_del){
                    store("o" + it5->first, std::to_string(0));
                }else if(it5->second == op_add){
                    store("o" + it5->first, std::to_string(1));
                }else{
                    store("o" + it5->first, std::to_string(2));
                }
            }

            store("Kse",(char*)Kse);
            std::cout << "Total keyword: " << keyword_counter << std::endl;
            delete cs_db;
            std::cout << "Bye~ " << std::endl;
            KUPRF::clean();
        }

        int store(const std::string k, const std::string v) {
            rocksdb::Status s = cs_db->Delete(rocksdb::WriteOptions(), k);
            s = cs_db->Put(rocksdb::WriteOptions(), k, v);
            if (s.ok()) return 0;
            else return -1;
        }

        //update过程加密一个操作块
        int encrypt(std::string &L_out, std::string &cip_R, std::string &cip_D, std::string &cip_C, OpType op,
                    const std::string &keyword, const int ind){
            unsigned char buf1[256], buf2[256], buf_D[256], buf_K1[256], buf_S1[256], buf_R[256];
            OpType op1;
            unsigned char op_ch;
            string s_K1, s_S1, s_R1, value;
            int id1;
            KUPRF kuprf;

            if (op == op_add)
                op_ch = 0x0f;
            else if (op == op_del)
                op_ch = 0xf0;
            else
                op_ch = 0xff;

            if (this->LastK.find(keyword) == this->LastK.end())
            {
                RAND_bytes(buf_S1, 16);
                kuprf.key_gen(buf_K1);
                s_S1.assign((char *) buf_S1, 16);
                s_K1.assign((char *) buf_K1, 32);
                this->LastK[keyword] = s_K1; 
                this->LastS[keyword] = s_S1;
            }
            else
            {
                s_K1 = this->LastK[keyword];
                s_S1 = this->LastS[keyword];
                memcpy(buf_K1, (const unsigned char *) s_K1.c_str(), 32);
                memcpy(buf_S1, (const unsigned char *) s_S1.c_str(), 16);
            }

            RAND_bytes(buf_R, 16);
            cip_R.assign((const char *) buf_R, 16);

            kuprf.Eval(buf2, buf_K1, keyword, ind, op);
            Hash_G(buf1, buf2, buf_R);

            L_out.assign((const char *) buf1, 32);
            Enc_id(cip_C, ind);

            PRF_F(buf1, buf_S1, keyword, ind, op);
            Hash_H(buf_D, 1 + 32 * 2 + 33, buf1, buf_R);

            buf_D[0] = buf_D[0] ^ op_ch;  //9

            if (this->LastOp.find(keyword) != this->LastOp.end())
            {
                id1 = this->LastId[keyword];
                op1 = this->LastOp[keyword];
                s_R1 = this->LastR[keyword];

                kuprf.Eval(buf1, buf_K1, keyword, id1, op1);
                Hash_G(buf2, buf1, (const unsigned char *) s_R1.c_str());
                Xor(32, buf_D + 1 + 33, buf2, buf_D + 1 + 33);

                PRF_F(buf1, buf_S1, keyword, id1, op1);
                Xor(32, buf_D + 1 + 33 + 32, buf1, buf_D + 1 + 33 + 32);

                if (op == op_del)
                {
                    kuprf.Eval(buf1, buf_K1, keyword, ind, op_add);
                    Xor(33, buf_D + 1, buf1, buf_D + 1);
                }
            }
            LastOp[keyword] = op;
            LastId[keyword] = ind;
            LastR[keyword] = cip_R;
            cip_D.assign((const char *) buf_D, 1 + 33 + 32 * 2);
            return 0;
        }

        vector<string> load_data_from_file(const std::string &filename){
            //这个函数可以通过读取数据集文件"sse_data_test",将数据存储到client内存中，rocksdb部分会在client析构的时候存进去
            string L, R, D, C;
            vector<string> serverEDB;
            FILE *f_data = fopen(filename.c_str(), "r");
            int counter = 0;
            int keyword_number = 0;
            //文件第一行，有多少个keyword
            fscanf(f_data, "%d\n", &keyword_number);
            for (int i = 0; i < keyword_number; i++){
                char word[512];
                fscanf(f_data, "%s\n", word);
                int file_numbers = 0;
                fscanf(f_data, "%d\n", &file_numbers);
                for (int j = 0; j < file_numbers; j++){
                    char name[6];
                    fscanf(f_data, "%s\n", name);
                    int int_name = stoi(name,0,16);
                    encrypt(L,R,D,C,op_add,word,int_name);
                    serverEDB.push_back(L);
                    serverEDB.push_back(R);
                    serverEDB.push_back(D);
                    serverEDB.push_back(C);
                }
                
            }
            fclose(f_data);
            return serverEDB;
        }

        std::string get_search_time(std::string w) {
            //std::string search_time = Util::hex2str("0000000000000000000000000000000000000000000000000000000000000000");
            int ind_len = AES::BLOCKSIZE / 2; // AES::BLOCKSIZE = 16
            byte tmp[ind_len] = {0};
            std::string search_time = /*Util.str2hex*/(std::string((const char *) tmp, ind_len));
            std::map<std::string, std::string>::iterator it;
            it = sc_mapper.find(w);
            if (it != sc_mapper.end()) {
                search_time = it->second;
            } else {
                set_search_time(w, search_time); // cache search_time into sc_mapper
            }
            return search_time;
        }

        int set_search_time(std::string w, std::string search_time) {
            //设置单词w更新次数为update_time
            {
                std::lock_guard <std::mutex> lock(sc_mtx);
                sc_mapper[w] = search_time;
            }
            // no need to store, because ti will be done in ~Client
            // store(w + "_search", std::to_string(search_time));
            return 0;
        }

//        void increase_search_time(std::string w) {
//            {
//                // std::lock_guard<std::mutex> lock(sc_mtx);
//                set_search_time(w, get_search_time(w) + 1);
//            }
//        }

        size_t get_update_time(std::string w) {
            size_t update_time = 0;
            std::map<std::string, size_t>::iterator it;
            it = uc_mapper.find(w);
            if (it != uc_mapper.end()) {
                update_time = it->second; // TODO need to lock when read, but for our scheme, no need
            } else {
                set_update_time(w, update_time);
            }
            return update_time;
        }

        int set_update_time(std::string w, int update_time) {
            {
                //std::lock_guard <std::mutex> lock(uc_mtx);
                std::mutex m;
                std::lock_guard <std::mutex> lockGuard(m);
                uc_mapper[w] = update_time;
            }
            return 0;
        }

        void increase_update_time(std::string w) {
            {
                //std::lock_guard<std::mutex> lock(uc_mtx);
                set_update_time(w, get_update_time(w) + 1);
            }
        }

        std::string gen_enc_token(const std::string token) {
            // 使用padding方式将所有字符串补齐到16的整数倍长度
            std::string token_padding;
            std::string enc_token;
            try {
                CFB_Mode<AES>::Encryption e;
                e.SetKeyWithIV(k_s, AES128_KEY_LEN, iv_s, (size_t) AES::BLOCKSIZE);
                token_padding = Util::padding(token);
                byte cipher_text[token_padding.length()];
                e.ProcessData(cipher_text, (byte *) token_padding.c_str(), token_padding.length());
                enc_token = std::string((const char *) cipher_text, token_padding.length());
            }
            catch (const CryptoPP::Exception &e) {
                std::cerr << "in gen_enc_token() " << e.what() << std::endl;
                exit(1);
            }
            return enc_token;
        }

        void gen_update_token(std::string op, std::string w, std::string ind, std::string &l, std::string &e) {
            try {
                std::string enc_token;
                std::string kw, tw;
                // get update time of `w` for `node`
                size_t uc;
                std::string sc;
                uc = get_update_time(w);
                sc = get_search_time(w);
                // tw = gen_enc_token(k_s, AES128_KEY_LEN, iv_s, w + "|" + std::to_string(-1) );
                tw = gen_enc_token(w);

                // generating update pair, which is (l, e)
                //l = Util::H1(tw + std::to_string(uc + 1));
                //e = Util::Xor(op + ind, Util::H2(tw + std::to_string(uc + 1)));
                l = Util::H1(tw + std::to_string(uc + 1));
                //l = Util::H1(tw + std::to_string(uc + 1));
                e = Util::Xor(op + ind, Util::H2(tw + std::to_string(uc + 1)));
                increase_update_time(w);
                set_search_time(w, Util::Xor(sc, ind));
            }
            catch (const CryptoPP::Exception &e) {
                std::cerr << "in gen_update_token() " << e.what() << std::endl;
                exit(1);
            }
        }

    //    UpdateRequestMessage gen_update_request(std::string op, std::string w, std::string ind, int counter) {
    //        try {
    //            std::string enc_token;
    //            UpdateRequestMessage msg;

    //            std::string kw, tw, l, e;
    //            // get update time of `w` for `node`
    //            size_t uc;
    //            std::string sc;
    //            uc = get_update_time(w);
    //            sc = get_search_time(w);
    //            tw = gen_enc_token(w);
    //            l = Util::H1(tw + std::to_string(uc + 1));
    //            e = Util::Xor(op + ind, Util::H2(tw + std::to_string(uc + 1)));
    //            msg.set_l(l);
    //            msg.set_e(e);
    //            msg.set_counter(counter);
    //            set_update_time(w, uc + 1); // TODO
    //            return msg;
    //        }
    //        catch (const CryptoPP::Exception &e) {
    //            std::cerr << "in gen_update_request() " << e.what() << std::endl;
    //            exit(1);
    //        }
    //    }

        UpdateRequestMessage gen_update_request_rose(string L,string R,string D,string C) {
            try {
                UpdateRequestMessage msg;
                msg.set_l(L);
                msg.set_r(R);
                msg.set_d(D);
                msg.set_c(C);
                // print_hex((unsigned char*)L.c_str(),L.length());
                // printf("\n");
                // print_hex((unsigned char*)R.c_str(),R.length());
                // printf("\n");
                // print_hex((unsigned char*)D.c_str(),D.length());
                // printf("\n");
                // print_hex((unsigned char*)C.c_str(),C.length());
                return msg;
            }
            catch (const CryptoPP::Exception &e) {
                std::cerr << "in gen_update_request_rose() " << e.what() << std::endl;
                exit(1);
            }
        }

//        // only used for simulation ...
//        CacheRequestMessage gen_cache_request(std::string keyword, std::string inds) {
//            try {
//                CacheRequestMessage msg;
//                std::string tw = gen_enc_token(keyword + "|" + std::to_string(-1));
//                msg.set_tw(tw);
//                msg.set_inds(inds);
//
//                return msg;
//            }
//            catch (const CryptoPP::Exception &e) {
//                std::cerr << "in gen_cache_request() " << e.what() << std::endl;
//                exit(1);
//            }
//        }

        void gen_search_token(std::string w, std::string &tw, size_t &uc) {
            try {
                std::string sc;
                uc = get_update_time(w);
                //sc = get_search_time(w);
                tw = gen_enc_token(w);
                //if (uc != 0) kw = gen_enc_token(tw + std::to_string(uc));
                //else kw = gen_enc_token(tw + "cache");
            } catch (const CryptoPP::Exception &e) {
                std::cerr << "in gen_search_token() " << e.what() << std::endl;
                exit(1);
            }
        }


        // 客户端RPC通信部分

        // std::string search(const std::string w) {
        //     logger::log(logger::INFO) << "client search(const std::string w):  " << std::endl;
        //     std::string tw;
        //     size_t uc;
        //     gen_search_token(w, tw, uc);
        //     search(tw, uc);
        //     // update `sc` and `uc`
        //     //increase_search_time(w);
        //     //set_update_time(w, 0);
        //     return "OK";
        // }

        int trapdoor(const string &keyword, string &tpd_L, string &tpd_T, string &cip_L, string &cip_R, string &cip_D, string &cip_C)
        {
            string s_R1, s_K1, s_S1,  s_K, s_S;
            int s_id1, ind_0;
            OpType op1;
            unsigned char buf1[256], buf2[256], buf_D[256], buf_R[256], buf_K1[256], buf_S1[256];
            unsigned char buf_K[256], buf_S[256];
            KUPRF kuprf;

            ind_0 = -1;

            s_id1 = this->LastId[keyword];
            op1 = this->LastOp[keyword];
            s_R1 = this->LastR[keyword];

            s_K1 = this->LastK[keyword];
            s_S1 = this->LastS[keyword];

            memcpy(buf_K1, (const unsigned char *) s_K1.c_str(), 32);
            memcpy(buf_S1, (const unsigned char *) s_S1.c_str(), 16);

            kuprf.Eval(buf1, buf_K1, keyword, s_id1, op1); //buf1 : P
            Hash_G(buf2, buf1, (const unsigned char *) s_R1.c_str()); //L'

            memcpy(buf_D + 1 + 33, buf2, 32);// 先插入L'
            tpd_L.assign((const char *) buf2, 32);// L'

            PRF_F(buf1, buf_S1, keyword, s_id1, op1);// T'
            memcpy(buf_D + 1 + 33 + 32, buf1, 32); //插入T'
            tpd_T.assign((const char *) buf1, 32);//T'

            kuprf.key_gen(buf_K);//K
            RAND_bytes(buf_S, 16);//S
            s_K.assign((const char *) buf_K, 32);
            s_S.assign((const char *) buf_S, 16);

            RAND_bytes(buf_R, 16);//R
            cip_R.assign((const char *) buf_R, 16);

            kuprf.update_token(buf_D + 1, buf_K, buf_K1);//delta t

            memset(buf1, 0, 64);
            kuprf.Eval(buf2, buf_K, keyword, ind_0, op_srh);//buf2 : P
            Hash_G(buf1, buf2, buf_R);// L
            cip_L.assign((const char *) buf1, 32);

            PRF_F(buf1, buf_S, keyword, ind_0, op_srh); //T
            Hash_H(buf2, 1 + 32 * 2 + 33, buf1, buf_R); //D
            buf_D[0] = 0xff; 
            Xor(1 + 32 * 2 + 33, buf_D, buf2, buf_D);

            Enc_id(cip_C, ind_0);

            LastOp[keyword] = op_srh;
            LastId[keyword] = ind_0;
            LastR[keyword] = cip_R;

            LastK[keyword] = s_K;
            LastS[keyword] = s_S;

            cip_D.assign((const char *) buf_D, 1 + 32 * 2 + 33);

            return 0;
        }

        std::string search_rose(const std::string w) {
            logger::log(logger::INFO) << "client search_rose(const std::string w):  " << std::endl;
            string tpd_L, tpd_T, L, R, D, C;
            //search stage 1: generate trapdoor
            trapdoor(w, tpd_L, tpd_T, L, R, D, C);
            //search stage 2: find cipehrtexts
            search(tpd_L, tpd_T, L, R, D, C);
            return "OK";
        }

        std::string search(string tpd_L, string tpd_T, string L, string R, string D, string C) {
            // request包含 enc_token 和 st
            SearchRequestMessage request;
            //request.set_kw(kw);
            request.set_tpd_l(tpd_L);
            request.set_tpd_t(tpd_T);
            request.set_l(L);
            request.set_r(R);
            request.set_d(D);
            request.set_c(C);
            // Context for the client. It could be used to convey extra information to the server and/or tweak certain RPC behaviors.
            ClientContext context;
            // 执行RPC操作，返回类型为 std::unique_ptr<ClientReaderInterface<SearchReply>>
            std::unique_ptr <ClientReaderInterface<SearchReply>> reader = stub_->search_rose(&context, request);
            // 读取返回列表
            int counter = 0;
            SearchReply reply;
            std::unordered_set <std::string> result;
            while (reader->Read(&reply)) {
                //logger::log(logger::INFO) << reply.ind() << std::endl;
                counter++;
                result.insert(reply.ind());
            }
            logger::log(logger::INFO) << " search result counter: " << counter << std::endl;
            logger::log(logger::INFO) << " search set result: " << result.size() << std::endl;
            return "OK";
        }





//        std::string search_for_trace(const std::string w, int uc) { // only used for trace simulation
//            std::string kw, tw;
//            std::string sc = get_search_time(w);
//            tw = gen_enc_token(w);
//            if (uc != 0) kw = gen_enc_token(tw + "|" + std::to_string(uc));
//            else kw = gen_enc_token(tw + "|" + "cache");
//            search(kw, tw, uc);
//            // don't need to update sc and uc for trace simulation
//            // increase_search_time(w);
//            // set_update_time(w, 0);
//            return "OK";
//        }

        // std::string search(const std::string tw, int uc) {
        //     // request包含 enc_token 和 st
        //     SearchRequestMessage request;
        //     //request.set_kw(kw);
        //     request.set_tw(tw);
        //     request.set_uc(uc);
        //     // Context for the client. It could be used to convey extra information to the server and/or tweak certain RPC behaviors.
        //     ClientContext context;
        //     // 执行RPC操作，返回类型为 std::unique_ptr<ClientReaderInterface<SearchReply>>
        //     std::unique_ptr <ClientReaderInterface<SearchReply>> reader = stub_->search(&context, request);
        //     // 读取返回列表
        //     int counter = 0;
        //     SearchReply reply;
        //     std::unordered_set <std::string> result;
        //     while (reader->Read(&reply)) {
        //         logger::log(logger::INFO) << reply.ind() << std::endl;
        //         counter++;
        //         result.insert(Util::str2hex(reply.ind()));
        //     }
        //     logger::log(logger::INFO) << " search result: " << counter << std::endl;
        //     logger::log(logger::INFO) << " search set result: " << result.size() << std::endl;
        //     return "OK";
        // }

        // std::string search2(const std::string w) {
        //     logger::log(logger::INFO) << "client search(const std::string w):  " << std::endl;
        //     std::string tw;
        //     size_t uc;
        //     gen_search_token(w, tw, uc);
        //     std::unordered_set <std::string> result = search2(tw, uc);
        //     struct timeval t1, t2;
        //     gettimeofday(&t1, NULL);
        //     verify(w, result);
        //     gettimeofday(&t2, NULL);
        //     double verify_time = ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000.0;
        //     logger::log_benchmark() << " verify time (ms): " << verify_time << std::endl;
        //     logger::log(logger::INFO) << " verify time (ms): " << verify_time << std::endl;
        //     // update `sc` and `uc`
        //     //increase_search_time(w);
        //     //set_update_time(w, 0);
        //     return "OK";
        // }


        // std::unordered_set <std::string> search2(const std::string tw, int uc) {
        //     // request包含 enc_token 和 st
        //     SearchRequestMessage request;
        //     //request.set_kw(kw);
        //     request.set_tw(tw);
        //     request.set_uc(uc);
        //     // Context for the client. It could be used to convey extra information to the server and/or tweak certain RPC behaviors.
        //     ClientContext context;
        //     // 执行RPC操作，返回类型为 std::unique_ptr<ClientReaderInterface<SearchReply>>
        //     std::unique_ptr <ClientReaderInterface<SearchReply>> reader = stub_->search(&context, request);
        //     // 读取返回列表
        //     int counter = 0;
        //     SearchReply reply;
        //     std::unordered_set <std::string> result;
        //     while (reader->Read(&reply)) {
        //         logger::log(logger::INFO) << reply.ind() << std::endl;
        //         counter++;
        //         result.insert(Util::str2hex(reply.ind()));
        //     }
        //     logger::log(logger::INFO) << " search result: " << counter << std::endl;
        //     logger::log(logger::INFO) << " search set result: " << result.size() << std::endl;
        //     return result;
        // }

        void verify(const std::string w, std::unordered_set <std::string> result) {
            int ind_len = AES::BLOCKSIZE / 2; // AES::BLOCKSIZE = 16
            byte tmp[ind_len] = {0};
            std::string Hw = /*Util.str2hex*/(std::string((const char *) tmp, ind_len));
            //std::string((const char *) tmp, ind_len);
            //std::cout << tmp << std::endl;
            for (std::unordered_set<std::string>::iterator i = result.begin(); i != result.end(); i++) {
                Hw = Util::Xor(Hw, *i);
            }
            logger::log(logger::INFO) << " H " << Hw << std::endl;
            std::string proof = get_search_time(w);
            logger::log(logger::INFO) << " proof " << proof << std::endl;
        }

//        Status update(UpdateRequestMessage update) {
//            ClientContext context;
//            ExecuteStatus exec_status;
//            // 执行RPC
//            Status status = stub_->update(&context, update, &exec_status);
//            // if(status.ok()) increase_update_time(w);
//            return status;
//        }
//
        // Status update(std::string op, std::string w, std::string ind) {
        //     logger::log(logger::INFO) << "client update(op, w, ind):  " << std::endl;
        //     ClientContext context;
        //     ExecuteStatus exec_status;
        //     // 执行RPC
        //     std::string l, e;
        //     gen_update_token(op, w, ind, l, e); // update(op, w, ind, _l, _e);
        //     UpdateRequestMessage update_request;
        //     update_request.set_l(l);
        //     update_request.set_e(e);
        //     Status status = stub_->update(&context, update_request, &exec_status);
        //     // if(status.ok()) increase_update_time(w);
        //     return status;
        // }

        // Status batch_update(std::vector <UpdateRequestMessage> update_list) {
        //     UpdateRequestMessage request;
        //     ClientContext context;
        //     ExecuteStatus exec_status;
        //     std::unique_ptr <ClientWriterInterface<UpdateRequestMessage>> writer(
        //             stub_->batch_update(&context, &exec_status));
        //     int i = 0;
        //     while (i < update_list.size()) {
        //         writer->Write(update_list[i]);
        //     }
        //     writer->WritesDone();
        //     Status status = writer->Finish();
        //     return status;
        // }


        // Status batch_update(std::string keyword, size_t N_entries) {
        //     logger::log(logger::INFO) << "client batch_update(std::string keyword, size_t N_entries)" << std::endl;
        //     //std::string id_string = std::to_string(thread_id);
        //     CryptoPP::AutoSeededRandomPool prng;
        //     int ind_len = AES::BLOCKSIZE / 2; // AES::BLOCKSIZE = 16
        //     byte tmp[ind_len];
        //     // for gRPC
        //     UpdateRequestMessage request;
        //     ClientContext context;
        //     ExecuteStatus exec_status;
        //     std::unique_ptr <ClientWriterInterface<UpdateRequestMessage>> writer(
        //             stub_->batch_update(&context, &exec_status));
        //     for (size_t i = 0; i < N_entries; i++) {
        //         prng.GenerateBlock(tmp, sizeof(tmp));
        //         std::string ind = /*Util.str2hex*/(std::string((const char *) tmp, ind_len));
        //         writer->Write(gen_update_request("1", keyword, ind));
        //     }
        //     // now tell server we have finished
        //     writer->WritesDone();
        //     Status status = writer->Finish();
        //     if (status.ok()) {
        //         //set_update_time(keyword, get_update_time(keyword) + N_entries);
        //         std::string log = "Random DB generation: completed: " + std::to_string(N_entries) + " keyword-filename";
        //         logger::log(logger::INFO) << log << std::endl;
        //     }
        //     return status;
        // }

//        void test_upload(int wsize, int dsize) {
//            std::string l, e;
//            for (int i = 0; i < wsize; i++)
//                for (int j = 0; j < dsize; j++) {
//                    gen_update_token("1", std::to_string(i), std::to_string(j), l, e); // update(op, w, ind, _l, _e);
//                    UpdateRequestMessage update_request;
//                    update_request.set_l(l);
//                    update_request.set_e(e);
//                    // logger::log(logger::INFO) << "client.test_upload(), l:" << l <<std::endl;
//                    Status s = update(update_request); // TODO
//                    // if (s.ok()) increase_update_time( std::to_string(i) );
//
//                    if ((i * dsize + j) % 1000 == 0)
//                        logger::log(logger::INFO) << " updating :  " << i * dsize + j << "\r" << std::flush;
//                }
//        }

    };

} // namespace DistSSE

#endif // DISTSSE_CLIENT_H
