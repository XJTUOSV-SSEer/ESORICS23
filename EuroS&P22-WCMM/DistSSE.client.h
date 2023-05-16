/* 
 * Created by Xiangfu Song on 10/21/2016.
 * Email: bintasong@gmail.com
 * 
 */
#ifndef DISTSSE_CLIENT_H
#define DISTSSE_CLIENT_H

#include <grpc++/grpc++.h>
#include <unordered_set>
#include "DistSSE.grpc.pb.h"
#include "DistSSE.Util.h"
#include "logger.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderInterface;
using grpc::ClientWriterInterface;
using grpc::ClientAsyncResponseReaderInterface;

using grpc::Status;

using namespace CryptoPP;


// 用来生成 kw
byte k_s[17] = "0123456789abcdef";
byte iv_s[17] = "0123456789abcdef";

byte k_t[17] = "abcdef1234567890";
byte iv_t[17] = "0123456789abcdef";

byte k_p[17] = "123456789abcdef0";
byte iv_p[17] = "0abcdef123456789";


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

namespace DistSSE{

class Client {

private:
 	std::unique_ptr<RPC::Stub> stub_;
	rocksdb::DB* cs_db;
	
	std::mutex sc_mtx;
	std::mutex uc_mtx;

	std::map<std::string, size_t> sc_mapper;	
	std::map<std::string, size_t> uc_mapper;

	std::map<std::string, size_t> WC1;
	std::map<std::string, size_t> WC2;

public:

  	Client(std::shared_ptr<Channel> channel, std::string db_path) : stub_(RPC::NewStub(channel)){
		rocksdb::Options options;
		// Util::set_db_common_options(options);
		
		// set options for merge operation
		rocksdb::Options simple_options;
		simple_options.create_if_missing = true;
		simple_options.merge_operator.reset(new rocksdb::StringAppendOperator() );
		simple_options.use_fsync = true;

    	rocksdb::Status status = rocksdb::DB::Open(simple_options, db_path , &cs_db);

		// load all sc, uc to memory
		rocksdb::Iterator* it = cs_db->NewIterator(rocksdb::ReadOptions());
		std::string key;
		size_t counter = 0;
	  	for (it->SeekToFirst(); it->Valid(); it->Next()) {
			key = it->key().ToString(); 
			if (key[0] == '1') { 
				WC1[key.substr(1, key.length() - 1)] = std::stoi(it->value().ToString());
			}
			else if(key[0] == '2'){
				WC2[key.substr(1, key.length() - 1)] = std::stoi(it->value().ToString());		
			}
			counter++;
	  	}

	  	// assert( it->status().ok() ); // Check for any errors found during the scan
		/*if(it->status().ok() == 0 ) */{
			std::cout<< "client db status: " <<it->status().ToString() <<std::endl;
		} 
	  	delete it;

		std::cout << "Just remind, previous keyword counter: "<< counter/2 <<std::endl;
	}

    ~Client() {

		// must store 'sc' and 'uc' to disk 

		size_t keyword_counter = 0;
		std::map<std::string, size_t>::iterator it;
		for (it = WC1.begin(); it != WC1.end(); ++it) {
			store("1" + it->first, std::to_string(it->second));
			keyword_counter++;
		}
		
		for (it = WC2.begin(); it != WC2.end(); ++it) {
			store("2" + it->first, std::to_string(it->second));
		}
		std::cout<< "Total keyword: " << keyword_counter <<std::endl;

		delete cs_db;

		std::cout<< "Bye~ " <<std::endl;
	}
	

	int store(const std::string k, const std::string v){
		rocksdb::Status s = cs_db->Delete(rocksdb::WriteOptions(), k);
		s = cs_db->Put(rocksdb::WriteOptions(), k, v);
		if (s.ok())	return 0;
		else return -1;
	}

	std::string get(const std::string k) {
		std::string tmp;
		rocksdb::Status s = cs_db->Get(rocksdb::ReadOptions(), k, &tmp);
		if (s.ok())	return tmp;
		else return "";
	}

	int get_search_time(std::string w){

		int search_time = 0;
		
		std::map<std::string, size_t>::iterator it;		
		
		it = sc_mapper.find(w);
		
		if (it != sc_mapper.end()) {
			search_time = it->second;
		}
		else {
			// std::string value = get("s" + w );

			// search_time = value == "" ? 0 : std::stoi(value);

			set_search_time(w, search_time); // cache search_time into sc_mapper 
		}
		return 0;//search_time;
	}

	int set_search_time(std::string w, int search_time) {
		//设置单词w更新次数为update_time
        {
		    std::lock_guard<std::mutex> lock(sc_mtx);		
			sc_mapper[w] = search_time;
		}
		// no need to store, because ti will be done in ~Client
		// store(w + "_search", std::to_string(search_time)); 
		return 0;
	}

	void increase_search_time(std::string w) {
		{
			// std::lock_guard<std::mutex> lock(sc_mtx);
			set_search_time(w, get_search_time(w) + 1);
		}
	}

	size_t get_update_time(std::string w){

		size_t update_time = 0;

		std::map<std::string, size_t>::iterator it;

		it = uc_mapper.find(w);
		
		if (it != uc_mapper.end()) {
			update_time = it->second; // TODO need to lock when read, but for our scheme, no need
		}
		else{
			// std::string value = get("u" + w );

			// update_time = value == "" ? 0 : std::stoi(value);
			
			set_update_time(w, update_time);
		}
		return update_time;
	}

	int set_update_time(std::string w, int update_time){
		{
			std::lock_guard<std::mutex> lock(uc_mtx);
			uc_mapper[w] = update_time;
		}		
		return 0;
	}
	
	void increase_update_time(std::string w) {
		{	
			// std::lock_guard<std::mutex> lock(uc_mtx);
			set_update_time(w, get_update_time(w) + 1);
		}
	}

	std::string gen_enc_token(const std::string token, byte* k,byte* iv){
		// 使用padding方式将所有字符串补齐到16的整数倍长度
		std::string token_padding;
		std::string enc_token;
		try {
			
			CFB_Mode< AES >::Encryption e;
		
			e.SetKeyWithIV(k, AES128_KEY_LEN, iv, (size_t)AES::BLOCKSIZE);
		
			token_padding = Util::padding(token);
	
			byte cipher_text[token_padding.length()];

			e.ProcessData(cipher_text, (byte*) token_padding.c_str(), token_padding.length());
			
			enc_token = std::string((const char*)cipher_text, token_padding.length());

		}
		catch(const CryptoPP::Exception& e)
		{
			std::cerr << "in gen_enc_token() " << e.what()<< std::endl;
			exit(1);
		}
		//std::cout << "enc_token_length: "<<enc_token.length()<<std::endl;
		return enc_token;
	}

	std::string gen_enc_token(const std::string token){
		// 使用padding方式将所有字符串补齐到16的整数倍长度
		std::string token_padding;
		std::string enc_token;
		try {
			
			CFB_Mode< AES >::Encryption e;
		
			e.SetKeyWithIV(k_s, AES128_KEY_LEN, iv_s, (size_t)AES::BLOCKSIZE);
		
			token_padding = Util::padding(token);
	
			byte cipher_text[token_padding.length()];

			e.ProcessData(cipher_text, (byte*) token_padding.c_str(), token_padding.length());
			
			enc_token = std::string((const char*)cipher_text, token_padding.length());

		}
		catch(const CryptoPP::Exception& e)
		{
			std::cerr << "in gen_enc_token() " << e.what()<< std::endl;
			exit(1);
		}
		return enc_token;
	}


	void gen_update_token(std::string op, std::string w, std::string ind, std::string& l, std::string& e){
		try{
			std::string enc_token;
	
			std::string kw, tw;
			// get update time of `w` for `node`
			size_t sc, uc;
			uc = get_update_time(w);
			sc = get_search_time(w);

			// tw = gen_enc_token(k_s, AES128_KEY_LEN, iv_s, w + "|" + std::to_string(-1) );
			kw = gen_enc_token(w + "|" + std::to_string(sc) );

			// generating update pair, which is (l, e)
			l = Util::H1( kw + std::to_string(uc + 1) );
			e = Util::Xor( op + ind, Util::H2(kw + std::to_string(uc + 1)) );
			// increase_update_time(w);
			
		}
		catch(const CryptoPP::Exception& e) {
			std::cerr << "in gen_update_token() " << e.what() << std::endl;
			exit(1);
		}
	}

	UpdateRequestMessage gen_update_request(std::string op, std::string w, std::string ind){
		try{
			UpdateRequestMessage msg;
			std::string ut;
			std::string e;
			std::string proof;
			int c1,c2;
			if(WC1.find(w) == WC1.end() || WC2.find(w) == WC2.end()){
				c1 = 0;
				c2 = 0;
			}
			c1 = WC1[w];
			c2 = WC2[w];

			std::string s_w = gen_enc_token(w,k_s,iv_s);
			c1++;
			// std::string st = gen_enc_token(w+std::to_string(c1)+std::to_string(c2),k_t,iv_t);
			std::string st = Util::H1(w+std::to_string(c1)+std::to_string(c2));
			// std::cout<<st1<<std::endl;
			st = st.substr(0,16);
			// if(st.length() > 16){
			// 	st = st.substr(8,16);
			// }
			//std::cout<<"c1:"<<c1<<std::endl;
			if(c1 == 1){
				//std::cout<<Util::H2(s_w + st).length()<<std::endl;
				std::string zero = std::string(16,'0');
				// std::cout << "test_size:" << zero + op + ind << std::endl;
				e = Util::Xor(Util::H2(s_w + st),zero + op + ind);
			}else{
				std::string oldst = Util::H1(w+std::to_string(c1-1)+std::to_string(c2));
				oldst = oldst.substr(0,16);
				// std::string oldst = gen_enc_token(w + std::to_string(c1 - 1) + std::to_string(c2),k_t,iv_t);
				// if(st.length() > 16){
				// 	oldst = oldst.substr(8,16);
				// }
				// std::cout << "test_size:" << zero + op + ind << std::endl;
				//std::cout<<oldst.length()<<std::endl;//为什么前100个长度是16，后面都是32？
				e = Util::Xor(Util::H2(s_w + st),oldst + op + ind);
			}
			std::string k_w = gen_enc_token(w + std::to_string(c2), k_p, iv_p);
			proof = Util::Enc(k_w.c_str(), k_w.length(), op + ind);
			//Util::print_bytes((unsigned char*)k_w.c_str(),k_w.length());
			//Util::print_bytes((unsigned char*)proof.c_str(),proof.length());

			WC1[w] = c1;
			WC2[w] = c2;
			ut = Util::H1(s_w+st);

			msg.set_ut(ut);
			msg.set_e(e);
			msg.set_proof(proof);	
			return msg;
		}
		catch(const CryptoPP::Exception& e){
			std::cerr << "in gen_update_request() " << e.what() << std::endl;
			exit(1);
		}
	}

	// only used for simulation ...
	CacheRequestMessage gen_cache_request(std::string keyword, std::string inds){
		try{
	
			CacheRequestMessage msg;
			std::string tw = gen_enc_token( keyword + "|" + std::to_string(-1) );
			msg.set_tw(tw);
			msg.set_inds(inds);

			return msg;
		}
		catch(const CryptoPP::Exception& e){
			std::cerr << "in gen_cache_request() " << e.what() << std::endl;
			exit(1);
		}
	}

	SearchRequestMessage gen_search_token(std::string w){
		try{
			SearchRequestMessage request;
			std::string s_w = gen_enc_token(w,k_s,iv_s);
			if (WC1.find(w) == WC1.end()){
				return request;
			}
			int c1 = WC1[w];
			int c2 = WC2[w];
			// std::string st = gen_enc_token(w + std::to_string(c1) + std::to_string(c2) , k_t ,iv_t);
			std::string st = Util::H1(w+std::to_string(c1)+std::to_string(c2));
			// std::cout<<st1<<std::endl;
			st = st.substr(0,16);
			// if(st.length() > 16){
			// 	st = st.substr(8,16);
			// }
		
			request.set_s_w(s_w);
			request.set_st(st);
			request.set_c1(c1);
			request.set_c2(c2);
			return request;
		}catch(const CryptoPP::Exception& e){
			std::cerr << "in gen_search_token() " <<e.what() << std::endl;
			exit(1);
		}
	}

	void gen_search_token(std::string w, std::string& kw, std::string& tw, size_t& uc) {
		try{
			// get update time of
			int sc;
			uc = get_update_time(w);
			sc = get_search_time(w);

			tw = gen_enc_token( w + "|" + std::to_string(-1) );
			if(uc != 0)	kw = gen_enc_token( w + "|" + std::to_string(sc) );
			else kw = gen_enc_token( w + "|" + "cache" );
			//else kw = "";
			
		}
		catch(const CryptoPP::Exception& e){
			std::cerr << "in gen_search_token() " <<e.what() << std::endl;
			exit(1);
		}
	}

// 客户端RPC通信部分

	// std::string search(const std::string w) {
	// 	std::string kw, tw;
	// 	size_t uc;
	// 	gen_search_token(w, kw, tw, uc);
		

	// 	search(kw, tw, uc);

	// 	// update `sc` and `uc`
	// 	increase_search_time(w);
	// 	set_update_time(w, 0);
	// 	return "OK";
	// }

	// std::string search_for_trace(const std::string w, int uc) { // only used for trace simulation
	// 	std::string kw, tw;

	// 	int sc = get_search_time(w);

	// 	tw = gen_enc_token( w + "|" + std::to_string(-1) );
	// 	if(uc != 0)	kw = gen_enc_token( w + "|" + std::to_string(sc) );
	// 	else kw = gen_enc_token( w + "|" + "cache" );
		
	// 	search(kw, tw, uc);

	// 	// don't need to update sc and uc for trace simulation
	// 	// increase_search_time(w);
	// 	// set_update_time(w, 0);
	// 	return "OK";
	// }

	std::string search(std::string w) {
		// Context for the client. It could be used to convey extra information to the server and/or tweak certain RPC behaviors.
		logger::log(logger::INFO) << "in search" << std::endl;
		SearchRequestMessage request = gen_search_token(w);
		ClientContext context;
		// 执行RPC操作，返回类型为 std::unique_ptr<ClientReaderInterface<SearchReply>>
		struct timeval t1,t2;
		gettimeofday(&t1, NULL);
		SearchReply reply;
		std::unique_ptr<ClientReaderInterface<SearchReply>> reader = stub_->search(&context, request);
		// 读取返回列表
		int counter = 0;
		std::vector<std::string> P;
		std::unordered_set<std::string> R;
		//std::vector<std::string> R;
		//std::cout<<reply.proof_size()<<std::endl;
		// for(int i=0;i<reply.proof_size();i++){
		// 	//Util::print_bytes((unsigned char*)reply.proof(i).c_str(),reply.proof(i).length());
		// 	P.push_back(reply.proof(i));
		// }
		// //std::cout<<reply.ind_size()<<std::endl;
		// for(int i=0;i<reply.ind_size();i++){
		// 	R.insert(reply.ind(i));
		// 	counter++;
		// }
		while(reader->Read(&reply)){
			if(reply.ind() != ""){
				R.insert(reply.ind());
				//R.push_back(reply.ind());
			}
			if(reply.proof() != ""){
				P.push_back(reply.proof());
			}
		}
		gettimeofday(&t2, NULL);
		//输出到日志文件
		logger::log_benchmark()<<"search time: "
							<< ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000.0 << " ms"
							<< std::endl;
		//输出到终端
		logger::log(logger::INFO)<<"search time: "
								<< ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000.0 << " ms"
								<< std::endl;
		//verify
		std::cout<<"P: "<<P.size()<<std::endl;
		std::cout<<"R: "<<R.size()<<std::endl;

		struct timeval t3,t4;
		gettimeofday(&t3, NULL);
		

		if(verify(P,R,w) == "Reject"){
			return "Reject";
		}else{
			//std::cout<< "test" <<std::endl;
			if(P.size() -1 <= R.size()){
				logger::log(logger::INFO) << " search result: "<< counter<< std::endl;
			}else{
				ReProof(R,w);
				logger::log(logger::INFO) << " search result: "<< counter<< std::endl;
			}
		}
		gettimeofday(&t4, NULL);
		logger::log_benchmark()<<"verify time: "
							<< ((t4.tv_sec - t3.tv_sec) * 1000000.0 + t4.tv_usec - t3.tv_usec) / 1000.0 << " ms"
							<< std::endl;
		//输出到终端
		logger::log(logger::INFO)<<"verify time: "
								<< ((t4.tv_sec - t3.tv_sec) * 1000000.0 + t4.tv_usec - t3.tv_usec) / 1000.0 << " ms"
								<< std::endl;
		
		logger::log_benchmark()<<"total time: "
							<< ((t4.tv_sec - t1.tv_sec) * 1000000.0 + t4.tv_usec - t1.tv_usec) / 1000.0 << " ms"
							<< std::endl;
		//输出到终端
		logger::log(logger::INFO)<<"total time: "
								<< ((t4.tv_sec - t1.tv_sec) * 1000000.0 + t4.tv_usec - t1.tv_usec) / 1000.0 << " ms"
								<< std::endl;
		return "Accept";
	}

	Status ReProof(std::unordered_set<std::string> R,std::string w){
		logger::log(logger::INFO) << "in ReProof" << std::endl;
		int c1 = WC1[w];
		int c2 = WC2[w];
		c1 = 1;
		c2 ++;
		// std::string st = gen_enc_token(w + std::to_string(c1) + std::to_string(c2), k_t, iv_t);
		std::string st = Util::H1(w+std::to_string(c1)+std::to_string(c2));
			// std::cout<<st1<<std::endl;
		st = st.substr(0,16);
		// if(st.length() > 16){
		// 	st = st.substr(8,16);
		// }
		// std::cout<<"st: ";
		// Util::print_bytes((unsigned char*)st.c_str(),st.length());
		std::string R_str = "";
		for(auto iter = R.begin(); iter!=R.end(); iter++){
			R_str += *iter;
		}

		std::string e = Util::Enc(st.c_str(),st.length(),R_str);
		std::cout<<"e length:"<<e.size()<<std::endl;
		// std::string M = Util::Dec(st.c_str(),st.length(),e);
		// std::cout<<"M length:"<<M.size()<<std::endl;
		std::string k_w = gen_enc_token(w + std::to_string(c2), k_p, iv_p);
		std::string proof = Util::Enc(k_w.c_str(),k_w.length(),R_str);
		WC1[w] = c1;
		WC2[w] = c2; 
		std::string s_w = gen_enc_token(w, k_s, iv_s);
		std::string ut = Util::H1(s_w + st);

		ReProofRequestMessage msg;
		msg.set_ut(ut);
		msg.set_proof(proof);
		msg.set_e(e);
		ClientContext context;
		ExecuteStatus exec_status;
		Status status = stub_->ReProof(&context, msg, &exec_status);
		logger::log(logger::INFO) << "ReProof finish" << std::endl;
		return status;
	}

	std::string verify(std::vector<std::string> P,std::unordered_set<std::string> R,std::string w){
		logger::log(logger::INFO) << "in verify" << std::endl;
		std::unordered_set<std::string> R_prime;
		int c1 = WC1[w];
		int c2 = WC2[w];
		//P多一个
		// std::cout<<"P: "<<P.size()<<std::endl;
		// std::cout<<"c1: "<<c1<<std::endl;
		if(P.size() - 1 != c1){
			return "Reject";
		}
		std::string k_w = gen_enc_token(w + std::to_string(c2), k_p, iv_p);
		//Util::print_bytes((unsigned char*)k_w.c_str(),k_w.length());
		//Util::print_bytes((unsigned char*)P[1].c_str(),P[1].length());
		std::string r = Util::Dec(k_w.c_str(),k_w.length(),P[1]);
		if(c2 > 0){
			//r是一个集合
			for(int i=0;i<r.length();i+=15){
				std::string id = r.substr(i,15);
				R_prime.insert(id);
			}
		}else{
			//r = op||id
			//std::cout<< r[0] <<std::endl;
			if(r.substr(0,1) == "1"){
				R_prime.insert(r.substr(1,15));
			}
		}
		for(int i=2;i<=c1;i++){
			r = Util::Dec(k_w.c_str(),k_w.length(),P[i]);
			if(r.substr(0,1) == "1"){
				R_prime.insert(r.substr(1,15));
			}else if(r.substr(0,1) == "0"){
				R_prime.erase(r.substr(1,15));
			}
		}
		// std::cout<<R.size()<<std::endl;
		// std::cout<<R_prime.size()<<std::endl;
		if(set_equal(R,R_prime)){
			std::cout<<"Accept"<<std::endl;
			return "Accept";
		}else{
			return "Reject";
		}
	}

	bool set_equal(std::unordered_set<std::string> s1, std::unordered_set<std::string> s2){
		if(s1.size() != s2.size()){
			return false;
		}
		for(auto iter = s1.begin();iter!=s1.end();iter++){
			if(s2.find(*iter) == s2.end()){
				return false;
			}
		}
		return true;
	}

	// std::string search(const std::string kw, const std::string tw, int uc) {
	// 	// request包含 enc_token 和 st
	// 	SearchRequestMessage request;
	// 	request.set_kw(kw);
	// 	request.set_tw(tw);
	// 	request.set_uc(uc);

	// 	// Context for the client. It could be used to convey extra information to the server and/or tweak certain RPC behaviors.
	// 	ClientContext context;

	// 	// 执行RPC操作，返回类型为 std::unique_ptr<ClientReaderInterface<SearchReply>>
	// 	std::unique_ptr<ClientReaderInterface<SearchReply>> reader = stub_->search(&context, request);
		
	// 	// 读取返回列表
	// 	int counter = 0;
	// 	SearchReply reply;
	// 	while (reader->Read(&reply)){
	// 		// logger::log(logger::INFO) << reply.ind()<<std::endl;
	// 		counter++;
	// 	}
	// 	// logger::log(logger::INFO) << " search result: "<< counter << std::endl;
	// 	return "OK";
	// }

	Status update(UpdateRequestMessage update) {

		ClientContext context;

		ExecuteStatus exec_status;
		// 执行RPC
		Status status = stub_->update(&context, update, &exec_status);
		// if(status.ok()) increase_update_time(w);

		return status;
	}

	// Status update(std::string op, std::string w, std::string ind) {
	// 	ClientContext context;

	// 	ExecuteStatus exec_status;
	// 	// 执行RPC
	// 	std::string l, e;
	// 	gen_update_token(op, w, ind, l, e); // update(op, w, ind, _l, _e);
	// 	UpdateRequestMessage update_request;
	// 	update_request.set_l(l);
	// 	update_request.set_e(e);

	// 	Status status = stub_->update(&context, update_request, &exec_status);
	// 	// if(status.ok()) increase_update_time(w);

	// 	return status;
	// }

	Status batch_update(std::vector<UpdateRequestMessage> update_list) {
		UpdateRequestMessage request;

		ClientContext context;

		ExecuteStatus exec_status;

		std::unique_ptr<ClientWriterInterface<UpdateRequestMessage>> writer(stub_->batch_update(&context, &exec_status));
		int i = 0;		
		while(i < update_list.size()){
			writer->Write(update_list[i]);
		}
		writer->WritesDone();
	    Status status = writer->Finish();

		return status;
	}

	// void test_upload( int wsize, int dsize ){
	// 	std::string l,e;
	// 	for(int i = 0; i < wsize; i++)
	// 		for(int j =0; j < dsize; j++){
	// 			gen_update_token("1", std::to_string(i), std::to_string(j), l, e); // update(op, w, ind, _l, _e);
	// 			UpdateRequestMessage update_request;
	// 			update_request.set_l( l );
	// 			update_request.set_e( e );
	// 			// logger::log(logger::INFO) << "client.test_upload(), l:" << l <<std::endl;
	// 			Status s = update( update_request ); // TODO
	// 			// if (s.ok()) increase_update_time( std::to_string(i) );

	// 			if ( (i * dsize + j) % 1000 == 0) logger::log(logger::INFO) << " updating :  "<< i * dsize + j << "\r" << std::flush;
	// 		}
	// }

};

} // namespace DistSSE

#endif // DISTSSE_CLIENT_H
