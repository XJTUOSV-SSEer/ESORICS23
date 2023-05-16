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
#include <unordered_set>



#define min(x ,y) ( (x) < (y) ? (x) : (y) )
#define max(x, y) ( (x) < (y) ? (y) : (x) )

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;


namespace DistSSE{

class DistSSEServiceImpl final : public RPC::Service {
private:	
	static rocksdb::DB* ss_db;
	// static rocksdb::DB* ss_db_read;
	static rocksdb::DB* cache_db;
    int MAX_THREADS;

	static std::mutex result_mtx;
	//static std::mutex cache_write_mtx;

public:
	DistSSEServiceImpl(const std::string db_path, std::string cache_path, int concurrent){

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
		simple_options.merge_operator.reset(new rocksdb::StringAppendOperator() );
		simple_options.use_fsync = true;

		rocksdb::Status s3 = rocksdb::DB::Open(simple_options, cache_path, &cache_db);

		if(!s1.ok()) {
			std::cerr<< "open ssdb error:"<<s1.ToString() <<std::endl;	
		}
		// assert(s2.ok());
		assert(s3.ok());

		MAX_THREADS = concurrent; //std::thread::hardware_concurrency();
	}

	static void abort( int signum )
	{
		delete ss_db;
		delete cache_db; 
		logger::log(logger::INFO)<< "abort: "<< signum <<std::endl;
	   	exit(signum);
	}

	static int store(rocksdb::DB* &db, const std::string l, const std::string e){
		rocksdb::Status s; 	
		rocksdb::WriteOptions write_option = rocksdb::WriteOptions();
		// write_option.sync = true;
		// write_option.disableWAL = false;
		{
			//std::lock_guard<std::mutex> lock(ssdb_write_mtx);		
			s = db->Put(write_option, l, e);
		}

		assert(s.ok());
		if (s.ok())	return 0;
		else return -1;
	}

	static std::string get(rocksdb::DB* &db, const std::string l){
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

	static bool get(rocksdb::DB* &db, const std::string l, std::string& e){
		rocksdb::Status s;
	
		{
			//std::lock_guard<std::mutex> lock(ssdb_write_mtx);
			s = db->Get(rocksdb::ReadOptions(), l, &e);
		}
		
		return s.ok(); 
	}

	static int merge(rocksdb::DB* &db, const std::string l, const std::string append_str) {

		int status = -1;
		
		try{

			rocksdb::WriteOptions write_option = rocksdb::WriteOptions();
			write_option.sync = true;

			rocksdb::Status s;
			{
				//std::lock_guard<std::mutex> lock(cache_write_mtx);
				s = db->Delete(write_option, l);
				s = db->Put(write_option, l, append_str);
			}

			if (s.ok())	status = 0;

		}catch(std::exception &e) {
			std::cerr << "in append_cache() " << e.what()<< std::endl;
			exit(1);
		}

		return status;
	}

	static int delete_entry(rocksdb::DB* &db, const std::string l) {
		int status = -1;
		
		try{

			rocksdb::WriteOptions write_option = rocksdb::WriteOptions();
			//write_option.sync = true;

			rocksdb::Status s;
				
			s = db->Delete(write_option, l);

			if (s.ok())	status = 0;

		}catch(std::exception &e) {
			std::cerr << "in delete_entry() " << e.what()<< std::endl;
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
		double per_entry_time = result_size == 0 ? search_time : (search_time/result_size);
		std::cout <<  word + "\t" + std::to_string(result_size)+ "\t" + std::to_string(search_time) + "\t" + std::to_string(per_entry_time) << std::endl;
	}

	static void parse (std::string str, std::string& op, std::string& ind) {
		op = str.substr(0, 1);		
		ind = str.substr(1, 7); // TODO
	}


	static void search_task(std::string kw, int begin, int max, int step, std::set<std::string>* result_set) {
		std::string ind, op;
		std::string l, e, value;

		bool flag = false;
		for(int i = begin ; i <= max; i += step) {

			l = Util::H1(kw + std::to_string(i));
			e = get(ss_db, l);
			if (e.compare("") == 0) {
				logger::log(logger::ERROR) << "ERROR in search, null str found: " << l << l.length() <<std::endl;
				continue;
			}
			
			value = Util::Xor( e, Util::H2(kw + std::to_string(i)) );

			parse(value, op, ind);
			
			{
				std::lock_guard<std::mutex> lock(result_mtx);
				if(op == "1") result_set->insert(value); // TODO
				// else result_set->erase(ind);
				// if (i % 1000 == 0) logger::log(logger::INFO) << "Thread ID: " << threadID << ", searched: " << i << "\n" <<std::flush;
 			}

			//  int s = delete_entry(ss_db, l);	
		}
	}


	static void search_single(std::string tw, std::string kw, int max, std::unordered_set<std::string>& cache_ID, std::unordered_set<std::string>& update_ID, std::string& cache_string, std::string& update_string) {
		std::string l, e, ind, op, value;
		// std::string cache_string, update_string;
		std::mutex res_mutex;

		auto read_cache_job = [&tw, &cache_ID, &cache_string] ( ) {
			cache_string = get(cache_db, tw);

			//res_mutex.lock();	
			Util::split(cache_string, '|', cache_ID);//TODO
			//res_mutex.unlock();
		};

		std::thread cache_thread(read_cache_job);

		for(int i = 1; i <= max; i++) {

			l = Util::H1(kw + std::to_string(i));

			e = get(ss_db, l);

			if ( e.compare("") == 0) {
				logger::log(logger::ERROR) << "we were supporsed to find something!"<<std::endl;
				continue;			
			}
			
			value = Util::Xor(e, Util::H2(kw + std::to_string(i)) );

			parse(value, op, ind);
			
			if(op == "1") {
				res_mutex.lock();	
				update_ID.insert(Util::str2hex(ind));
				res_mutex.unlock();	
			}

			update_string += Util::str2hex(ind) + "|";
		}

		cache_thread.join();
		//merge(cache_db, tw, cache_string + update_string);

	}

	static void merge( std::set<std::string>& searchResult,  std::string& merge_string){
	
			for (auto& t : searchResult) {
				merge_string += Util::str2hex(t) + "|";
			}
	}


	Status search(ServerContext *context, const SearchRequestMessage *request, ServerWriter <SearchReply> *writer) {
		logger::log(logger::INFO) << "server: search(ServerContext *context, const SearchRequestMessage *request, ServerWriter <SearchReply> *writer)"<< std::endl;
		struct timeval t1, t2;
		gettimeofday(&t1, NULL);
		//这里其实P[0]是多出来的
		std::vector<std::string> P(request->c1() + 1);
		std::unordered_set<std::string> Del;
		std::unordered_set<std::string> R;
		//std::vector<std::string> R;
		std::unordered_set<std::string> Uts;
		std::string ut; 
		int i = request->c1();
		//std::cout<<"i:"<<i<<std::endl;
		std::string s_w = request->s_w();
		std::string st = request->st();
		// std::cout<<"st: ";
		// Util::print_bytes((unsigned char*)st.c_str(),st.length());
		int c1 = request->c1();
		int c2 = request->c2();
		std::string e;
		std::string proof;
		int count = 0;
		P[0] = "#";
		while(i>1){
			ut = Util::H1(s_w + st);
			Uts.insert(ut);
			get(ss_db, "e" + ut, e);
			get(ss_db, "p" + ut, proof);
			P[i] = proof;
			//Util::print_bytes((unsigned char*)P[i].c_str(),P[i].length());
			std::string M = Util::Xor(e,Util::H2(s_w + st));//前16位为st，后一位为op, 后15位为id
			st = M.substr(0,16);
			std::string op = M.substr(16,1);
			std::string id = M.substr(17,15);
			std::cout<<"M:" << M<<std::endl;

			//std::cout<<id<<std::endl;
			//std::cout<<op<<std::endl;
			// if(Del.find(id) == Del.end()){
			// 	count ++;
			// }
			std::cout<< "id:" << id<<std::endl;
		
			// if(op == "1"){
			// 	count ++;
			// }
			if(op == "1" && Del.find(id) == Del.end()){
				R.insert(id);
				//R.push_back(id);
			}else if(op == "0"){
				Del.insert(id);
			}
			i--;
		}
		//i = 1
		ut = Util::H1(s_w + st);
		Uts.insert(ut);
		get(ss_db, "e" + ut, e);
		get(ss_db, "p" + ut, proof);
		P[i] = proof;
		//Util::print_bytes((unsigned char*)P[i].c_str(),P[i].length());
		if(c2 > 0){
			int number = 0;
			std::string old_R = Util::Dec(st.c_str(),st.length(),e);
			for(int i=0;i<old_R.length();i+=15){
				std::string id = old_R.substr(i,15);
				if(Del.find(id) == Del.end()){
					R.insert(id);
					//R.push_back(id);
				}
				number ++;
			}	
			//std::cout<<"old_R: "<<number<<std::endl;
		}else{
			std::string M = Util::Xor(e,Util::H2(s_w + st));
			std::string op = M.substr(16,1);
			std::string id = M.substr(17,15);
			
			//std::cout<<op<<std::endl;
			if(op == "1" && Del.find(id) == Del.end()){
				R.insert(id);
				//R.push_back(id);
			}

		}
		//P实际上多出一个
		if(P.size()-1 > R.size()){
			for(auto ut = Uts.begin(); ut != Uts.end();ut++){
				delete_entry(ss_db,"e" + *ut);
				delete_entry(ss_db,"l" + *ut);
			}
		}
		gettimeofday(&t2, NULL);
		// std::cout<<count<<std::endl;
		std::cout<<R.size()<<std::endl;
		std::cout<<P.size()<<std::endl;
		// for(auto iter_R = R.begin();iter_R != R.end();iter_R++){
		// 	reply->add_ind(*iter_R);
		// }
		// for(auto iter_P = P.begin();iter_P != P.end();iter_P++){
		// 	reply->add_proof(*iter_P);
		// }
		auto iter_R = R.begin();
		auto iter_P = P.begin();
		while(iter_R != R.end() || iter_P != P.end()){
			SearchReply reply;
			if(iter_R != R.end()){
				reply.set_ind(*iter_R);
				iter_R++;
			}else{
				reply.set_ind("");
			}
			if(iter_P != P.end()){
				reply.set_proof(*iter_P);
				iter_P++;
			}else{
				reply.set_proof("");
			}
			writer->Write(reply);
		}
		return Status::OK;
	}
	

	Status ReProof(ServerContext* context, const ReProofRequestMessage *request,ExecuteStatus* response){
		logger::log(logger::INFO) << "server ReProof(ServerContext* context, ServerReader<ReProofRequestMessage>* reader,ExecuteStatus* response)"<< std::endl;
		std::string ut;
		std::string e;
		std::string proof;
		ut = request->ut();
		e = request->e();
		proof = request->proof();
		//std::cout<<"e length:"<<e.size()<<std::endl;
		store(ss_db, "e" + ut, e);
		store(ss_db, "p" + ut, proof);
		response->set_status(true);
		return Status::OK;
	}
	
	// batch_update()实现批量更新操作
	Status batch_update(ServerContext* context, ServerReader< UpdateRequestMessage >* reader, ExecuteStatus* response) {
		logger::log(logger::INFO) << "server batch_update(ServerContext *context, const UpdateRequestMessage *request, ExecuteStatus *response): "<< std::endl;
		std::string ut;
		std::string e;
		std::string proof;

		UpdateRequestMessage request;
		while (reader->Read(&request)) {
			ut = request.ut();
			e = request.e();
			proof = request.proof();
			store(ss_db, "e" + ut, e);
			store(ss_db, "p" + ut, proof);
		}

		response->set_status(true);
		return Status::OK;
	}

	// batch_cache(), only used for expriment simulation
	Status batch_cache(ServerContext* context, ServerReader< CacheRequestMessage >* reader, ExecuteStatus* response) {
		std::string tw;
		std::string inds;

		CacheRequestMessage request;
		while (reader->Read(&request)){
			tw = request.tw();
			inds = request.inds();
			int s = merge(cache_db, tw, inds);
			assert(s == 0);
		}

		response->set_status(true);
		return Status::OK;
	}
};

}// namespace DistSSE

// static member must declare out of main function !!!
rocksdb::DB* DistSSE::DistSSEServiceImpl::ss_db;
// rocksdb::DB* DistSSE::DistSSEServiceImpl::ss_db_read;
rocksdb::DB* DistSSE::DistSSEServiceImpl::cache_db;

std::mutex DistSSE::DistSSEServiceImpl::result_mtx;
//std::mutex DistSSE::DistSSEServiceImpl::cache_write_mtx;

void RunServer(std::string db_path, std::string cache_path, int concurrent) {


  //std::string server_address("192.168.1.98:50051");
  std::string server_address("0.0.0.0:50051");
  DistSSE::DistSSEServiceImpl service(db_path, cache_path, concurrent);
  
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());

  DistSSE::logger::log(DistSSE::logger::INFO) << "Server listening on " << server_address << std::endl;

  server->Wait();
}

#endif // DISTSSE_SERVER_H
