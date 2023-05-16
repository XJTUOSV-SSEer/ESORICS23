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
#include <math.h>
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
        int N = 131072; //2^17
    

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

        //ROSE^2
        unsigned char K_f[16];
        unsigned char K_s[16];
        std::map <std::string, ST_value> ST;
        //temp
        map<pair<string,string>,int> OD_del;
        map<pair<string,int>,I_n> OD_tree;

    public:
        int get_Number(){
            return LastId.size();
        }

        Client(std::shared_ptr <Channel> channel, std::string db_path) : stub_(RPC::NewStub(channel)) {
            RAND_bytes(this->K_s, 16);
            RAND_bytes(this->K_f, 16);
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
                key = it->key().ToString();
                value = it->value().ToString();
                if(key[0] == 'u'){
                    ST[key.substr(1, key.length() - 1)].K_u = value;
                }else if(key[0] == 'k'){
                    ST[key.substr(1, key.length() - 1)].delta_k = value;
                }else if(key[0] == 'i'){
                    ST[key.substr(1, key.length() - 1)].cnt_i = std::stoi(value);
                }else if(key[0] == 'd'){
                    ST[key.substr(1, key.length() - 1)].cnt_d = std::stoi(value);
                }else if(key[0] == 's'){
                    ST[key.substr(1, key.length() - 1)].sn = std::stoi(value);
                }else if(key[0] == 'L'){
                    ST[key.substr(1, key.length() - 1)].lastL = value;
                }else if(key == "Ks"){
                    memcpy((char*)K_s, value.c_str(),16); //注意改一下rose
                }else if(key == "Kf"){
                    memcpy((char*)K_f, value.c_str(),16);
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
            std::map<std::string, ST_value>::iterator it;
            for(it = ST.begin(); it != ST.end(); it++){
                store("u"+it->first, it->second.K_u);
                store("k"+it->first, it->second.delta_k);
                store("i"+it->first, to_string(it->second.cnt_i));
                store("d"+it->first, to_string(it->second.cnt_d));
                store("s"+it->first, to_string(it->second.sn));
                store("L"+it->first, it->second.lastL);
                keyword_counter ++;
            }

            std::string K_s_str;
            std::string K_f_str;
            K_s_str.assign((char*)K_s,16);
            K_f_str.assign((char*)K_f,16);
            store("Ks", K_s_str);
            store("Kf", K_f_str);

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

        //计算sn
        int getSn_Rose_2(int cnt_i){
            int Sn = 0;
            int j = ceil(log2(cnt_i));
            Sn = ceil(cnt_i * 1.0 / pow(2,j)) + N/pow(2,j-1)*(pow(2,j)-1);
            return Sn;
        }
        std::vector<int> get_internal_nodes_Rose_2(int cnt_i){
            //cout<<cnt_i<<endl;
            std::vector<int> res;
            for(int j=log2(N);j>=1;j--){
                res.push_back(ceil(cnt_i * 1.0 / pow(2,j)) + N / pow(2,j-1) * (pow(2,j)-1));
                //cout<<ceil(cnt_i * 1.0 / pow(2,j)) + N / pow(2,j-1) * (pow(2,j)-1)<<endl;
            }
            return res;
        }
        
        OMAPFindMessage gen_findMessage(std::string w, std::string idorn, int flag){
            OMAPFindMessage findMessage;
            findMessage.set_w(w);
            findMessage.set_idorn(idorn);
            findMessage.set_flag(flag);
            return findMessage;
        }

        batchOMAPFindMessage gen_batchFindMessage(std::string w, vector<std::string> idorn, int flag){
            batchOMAPFindMessage batchFindMessage;
            batchFindMessage.set_w(w);
            for(int i=0;i<idorn.size();i++){
                batchFindMessage.add_idorn(idorn[i]);
            }
            batchFindMessage.set_flag(flag);
            return batchFindMessage;
        }

        OMAPInsertMessage gen_insertMessage(std::string w, std::string id, int flag, std::string value){
            OMAPInsertMessage insertMessage;
            insertMessage.set_w(w);
            insertMessage.set_idorn(id);
            insertMessage.set_flag(flag);
            insertMessage.set_value(value);
            return insertMessage;
        } 

        std::string gen_zero_L(int length){
            return string(length,'0');
        }

        int Update_Rose_2(std::string w, std::string id, int op){
            KUPRF kuprf;
            unsigned char buf_K_u[256];
            unsigned char buf_PRF[256];
            unsigned char buf_kuprf[256];
            std::string K_u;
            std::string delta_k;
            std::string PRF;
            int cnt_i;
            int cnt_d;
            int sn;
            std::string lastL;
            std::string L;
            std::string C;
            std::string D;
            OMAPFindMessage findMessage;
            OMAPFindReply findReply;
            OMAPInsertMessage insertMessage;
            //add
            if(op == 1){
                // if(OD_del.find(make_pair(w,id)) != OD_del.end()){
                //     return -1;
                // }
                findMessage = gen_findMessage(w,id,0);
                ClientContext context;
                stub_->OMAPFind(&context,findMessage,&findReply);
                if(findReply.value() != ""){
                    return -1;
                }
                if(ST.find(w) == ST.end()){
                    kuprf.key_gen(buf_K_u);
                    K_u.assign((char*) buf_K_u,32);
                    delta_k = K_u;
                    cnt_i = 0;
                    cnt_d = 0;
                    sn = 0;
                    lastL = gen_zero_L(33);
                }else{
                    K_u = ST[w].K_u;
                    delta_k = ST[w].delta_k;
                    cnt_i = ST[w].cnt_i;
                    cnt_d = ST[w].cnt_d;
                    sn = ST[w].sn;
                    lastL = ST[w].lastL;
                }
                cnt_i++;
                //OD_del[make_pair(w,id)] = cnt_i;
                insertMessage = gen_insertMessage(w,id,0,std::to_string(cnt_i));
                ClientContext context1;
                ExecuteStatus exec_status;
                stub_->OMAPInsert(&context1,insertMessage,&exec_status);
                sn = getSn_Rose_2(cnt_i);
                ST[w].K_u = K_u;
                ST[w].delta_k = delta_k;
                ST[w].cnt_i = cnt_i;
                ST[w].cnt_d = cnt_d;
                ST[w].sn = sn;
                
                PRF_F(buf_PRF, K_f, w);
                PRF.assign((char*) buf_PRF, 32);
                std::string hash = Util::H1(PRF+std::to_string(cnt_i)+"1");
                kuprf.Eval(buf_kuprf, (unsigned char*)K_u.c_str(), hash);
                L.assign((char*) buf_kuprf, 33);
                D = Util::Xor(hash + "0",lastL);
                //print_hex((unsigned char*)L.c_str(),L.length());
                C = Util::Enc(K_s,16,id);
                //send to server
                ClientContext context3;
                UpdateRequestMessage_Rose_2 request;
                request.set_l(L);
                request.set_c(C);
                request.set_d(D);
                stub_->update_Rose_2(&context3,request,&exec_status);
            }
            //del
            else if(op == 0){
                findMessage = gen_findMessage(w,id,0);
                ClientContext context;
                stub_->OMAPFind(&context,findMessage,&findReply);
                if(findReply.value() == ""){
                    return -1;
                }
                // if(OD_del.find(make_pair(w,id)) == OD_del.end()){
                //     return -1;
                // }
                K_u = ST[w].K_u;
                delta_k = ST[w].delta_k;
                cnt_i = ST[w].cnt_i;
                cnt_d = ST[w].cnt_d;
                sn = ST[w].sn;

                cnt_d++;
                cnt_i = atoi(findReply.value().c_str());
                std::vector<int> NN = get_internal_nodes_Rose_2(cnt_i);
                //batchFind N;
                batchOMAPFindMessage batchFindRequest;
                for(int i=0;i<NN.size();i++){
                    batchFindRequest.set_w(w);
                    batchFindRequest.add_idorn(to_string(NN[i]));
                    batchFindRequest.set_flag(1);
                }

                // 读取返回列表
                batchOMAPFindReply batchFindReply;
                ClientContext context1;
                stub_->batchOMAPFind(&context1,batchFindRequest,&batchFindReply);
                vector<I_n> Ins;
                for(int i=0;i<batchFindReply.value_size();i++){
                    I_n In;
                    if(batchFindReply.value(i) == ""){
                        In.Node = NN[i];
                        In.Num = 0;
                        In.lNum = 0;
                        In.rNum = 0;
                        if(i == NN.size()-1){
                            //如果是最后一个，那么左右子节点是叶子节点
                            In.lNode = cnt_i % 2 == 0?cnt_i - 1:cnt_i;
                            In.rNode = cnt_i % 2 == 0?cnt_i:cnt_i + 1;
                        }else{
                            In.lNode = NN[i+1] % 2 == 0?NN[i+1] - 1:NN[i+1];
                            In.rNode = NN[i+1] % 2 == 0?NN[i+1]:NN[i+1] + 1;
                        }
                    }else{
                        In = StringToIn(batchFindReply.value(i));
                    }
                    if(In.Num != -1){
                        Ins.push_back(In);
                    }
                }
                
                //TODO：需要更多的边界讨论，例如Ins.size()>=2
                I_n* p = &Ins[Ins.size()-1];
                I_n* gp = &Ins[Ins.size()-2];
                I_n sib;
                if(p->lNode == cnt_i){
                    sib.Node = p->rNode;
                    if(p->rNode <= N){
                        sib.Num = 0;
                    }else{
                        sib.Num = p->rNum;
                    }
                }else{
                    sib.Node = p->lNode;
                    if(p->lNode <= N){
                        sib.Num = 0;
                    }else{
                        sib.Num = p->lNum;
                    }
                }

                if(gp->Node > sn){
                    sn = gp->Node;
                }

                ST[w].cnt_d = cnt_d;
                ST[w].sn = sn;

                if(gp->lNode == p->Node){
                    gp->lNode = sib.Node;
                    gp->lNum = sib.Num;
                }else{
                    gp->rNode = sib.Node;
                    gp->lNum = sib.Num;
                }
                p = nullptr;
                gp = nullptr;

                //这里删除那个父节点p，即置-1
                Ins[Ins.size()-1].Num = -1;
                //将In重新写入OD_tree
                ClientContext context2;
                ClientContext context3;
                ExecuteStatus exec_status1;
                ExecuteStatus exec_status2;
                std::unique_ptr <ClientWriterInterface<OMAPInsertMessage>> writer(stub_->batchOMAPInsert(&context2, &exec_status1));
                std::unique_ptr <ClientWriterInterface<UpdateRequestMessage_Rose_2>> writer2(stub_->batch_update_Rose_2(&context3, &exec_status2));
                for(int i=0;i<Ins.size()-1;i++){
                    if(Ins[i].lNode == Ins[i+1].Node){
                        Ins[i].lNum++;
                    }else if(Ins[i].rNode == Ins[i+1].Node){
                        //这里的if中有可能会是叶子节点，所以不能直接else
                        Ins[i].rNum++;
                    }
                    Ins[i].Num++;
                    //OD_tree[make_pair(w,Ins[i].Node)] = Ins[i];
                    writer->Write(gen_insertMessage(w,to_string(Ins[i].Node),1,InToString(Ins[i])));
                    // cout<<"发送n:"<<Ins[i].Node<<endl;
                    // cout<<"value:"<<InToString(Ins[i])<<endl;
                    // cout<<"num:"<<Ins[i].Num<<endl;
                    PRF_F(buf_PRF, K_f, w);
                    PRF.assign((char*) buf_PRF, 32);
                    std::string hash = Util::H1(PRF + std::to_string(Ins[i].Node) + std::to_string(Ins[i].Num));
                    kuprf.Eval(buf_kuprf, (unsigned char*)K_u.c_str(), hash);
                    //print_hex((unsigned char*)K_u.c_str(),K_u.length());
                    L.assign((char*) buf_kuprf, 33);
                    //print_hex((unsigned char*)L.c_str(),L.length());
                    C = encrypt_Rose_2(hash ,Ins[i].lNode,Ins[i].lNum,Ins[i].rNode,Ins[i].rNum);
                    D = Util::Xor(hash + "0", gen_zero_L(33));
                    //send to server
                    UpdateRequestMessage_Rose_2 request = gen_update_request_Rose_2(L,C,D);
                    writer2->Write(request);
                }
                writer2->WritesDone();
                writer2->Finish();
                writer->Write(gen_insertMessage(w,to_string(Ins[Ins.size()-1].Node),1,InToString(Ins[Ins.size()-1])));//这是最后一个被删除的（置-1的）
                writer->WritesDone();
                writer->Finish();
            }
            return 0;
        }

        string encrypt_Rose_2(string in1, int lNode,int lNum,int rNode,int rNum){
            //总共长度32字节
            string res;
            res = to_string(lNode) + "*" + to_string(lNum) + "*" + to_string(rNode)+ "*" + to_string(rNum) + "*";
            int remain = 32 - res.length();
            res = res + string(remain,'#');
            res = Util::Xor(in1,res);
            return res;
        }

        string InToString(I_n In){
            string res;
            res = to_string(In.Node) + "*" + to_string(In.Num) + "*" +to_string(In.lNode) + "*" + to_string(In.lNum) + "*" + to_string(In.rNode) + "*" + to_string(In.rNum) + "*";
            return res;
        }

        I_n StringToIn(string InString){
            I_n In;
            vector<int> res;
            int index1 = 0;
            int index2 = 0;
            for(;index2<InString.length();index2++){
                if(InString[index2] == '*'){
                    res.push_back(atoi(InString.substr(index1,index2-index1).c_str()));
                    index1 = index2 + 1;
                }
            }
            In.Node = res[0];
            In.Num = res[1];
            In.lNode = res[2];
            In.lNum = res[3];
            In.rNode = res[4];
            In.rNum = res[5];
            return In;
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

        UpdateRequestMessage gen_update_request_rose(string L,string R,string D,string C) {
            try {
                UpdateRequestMessage msg;
                msg.set_l(L);
                msg.set_r(R);
                msg.set_d(D);
                msg.set_c(C);
                return msg;
            }
            catch (const CryptoPP::Exception &e) {
                std::cerr << "in gen_update_request_rose() " << e.what() << std::endl;
                exit(1);
            }
        }

        UpdateRequestMessage_Rose_2 gen_update_request_Rose_2(string L,string C,string D) {
            try {
                UpdateRequestMessage_Rose_2 msg;
                msg.set_l(L);
                msg.set_c(C);
                msg.set_d(D);
                return msg;
            }
            catch (const CryptoPP::Exception &e) {
                std::cerr << "in gen_update_request_Rose_2() " << e.what() << std::endl;
                exit(1);
            }
        }

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

        search_token trapdoor_Rose_2(string keyword){
            search_token st;
            KUPRF kuprf;
            unsigned char buf_K_u[256];
            unsigned char buf_delta_k[256];
            unsigned char buf_tk[256];
            kuprf.key_gen(buf_K_u);
            kuprf.update_token(buf_delta_k, (unsigned char*)ST[keyword].K_u.c_str(), buf_K_u);
            st.K_u = ST[keyword].K_u;
            ST[keyword].K_u.assign((const char *)buf_K_u,32);
            ST[keyword].delta_k.assign((const char *)buf_delta_k,32);
            
            PRF_F(buf_tk, K_f, keyword);
            st.delta_k = ST[keyword].delta_k;
            st.tk.assign((const char*)buf_tk, 32);
            st.sn = ST[keyword].sn;
            st.cnt_d = ST[keyword].cnt_d;
            st.cnt_i = ST[keyword].cnt_i;
            return st;
        }

        int trapdoor(const string &keyword, string &tpd_L, string &tpd_T, string &cip_L, string &cip_R, string &cip_D, string &cip_C){
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

        std::string search_Rose_2(const std::string w){
            struct timeval t1, t2;
            logger::log(logger::INFO) << "client search_Rose_2(const std::string w):  " << std::endl;
            search_token st = trapdoor_Rose_2(w);
            SearchRequestMessage_Rose_2 request;
            request.set_k_u(st.K_u);
            request.set_delta_k(st.delta_k);
            request.set_tk(st.tk);
            request.set_sn(st.sn);
            request.set_cnt_d(st.cnt_d);
            request.set_cnt_i(st.cnt_i);

            ClientContext context;
            std::unique_ptr <ClientReaderInterface<SearchReply_Rose_2>> reader = stub_->search_Rose_2(&context, request);
            int counter = 0;
            SearchReply_Rose_2 reply;
            std::unordered_set <std::string> result;
            gettimeofday(&t1, NULL);
            while (reader->Read(&reply)) {
                //logger::log(logger::INFO) << reply.ind() << std::endl;
                counter++;
                result.insert(reply.c());
            }
            logger::log(logger::INFO) << " search result counter: " << counter << std::endl;
            logger::log(logger::INFO) << " search set result: " << result.size() << std::endl;
            gettimeofday(&t2, NULL);
            //输出到日志文件
            logger::log_benchmark()<< "keyword: "+ w +" "+ std::to_string(counter)+" entries "+"update time: "
                               << ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000.0 << " ms"
                               << std::endl;
            //输出到终端
            logger::log(logger::INFO)<< "keyword: "+ w +" "+ std::to_string(counter)+" entries "+"update time: "
                                 << ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000.0 << " ms"
                                 << std::endl;
            return "OK";
        }

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

    };

} // namespace DistSSE

#endif // DISTSSE_CLIENT_H
