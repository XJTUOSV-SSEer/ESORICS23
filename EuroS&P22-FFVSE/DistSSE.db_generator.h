#include "DistSSE.client.h"
#include "DistSSE.Util.h"
#include <cmath>
#include <chrono>


namespace DistSSE{

		static std::mutex print_mtx;

		static bool sample(double value, double rate) {
			return (value - rate) < 0.0000000000000001 ? true : false;
		}
		
		static double rand_0_to_1(){ //
			return ((double) rand() / (RAND_MAX));		
		}

		static double rand_0_to_1(unsigned int seed){ 
			srand(seed);
			return ((double) rand() / (RAND_MAX));		
		}


		static void search_log(std::string word, int counter) {
		
				std::cout << word + "\t" + std::to_string(counter)<< std::endl;
		}

		static void padd(std::string& input,int length){
			if(input.length() < length){
				std::string pad(length - input.length(),'#');
				std::cout<<"pad:"<<pad<<std::endl;
				input = pad + input;
				std::cout<<"padd:"<<input<<std::endl;
			}
		}

		static void generation_job_Euro_repeat(Client* client,std::string keyword, unsigned int thread_id, size_t N_entries,double repeatRatio){
			struct timeval t1, t2;
			//std::string id_string = std::to_string(thread_id);
			CryptoPP::AutoSeededRandomPool prng;
			int ind_len = AES::BLOCKSIZE - 1 ; // AES::BLOCKSIZE = 16
			//std::unordered_set<std::string> s;
			byte tmp[ind_len];
			// for gRPC
			UpdateRequestMessage request;
			ClientContext context;
			ExecuteStatus exec_status;
			std::unique_ptr <RPC::Stub> stub_(RPC::NewStub(grpc::CreateChannel("0.0.0.0:50051", grpc::InsecureChannelCredentials())));
			std::unique_ptr <ClientWriterInterface<UpdateRequestMessage>> writer(stub_->batch_update(&context, &exec_status));
			for (size_t i = 0; i < N_entries * (1 - repeatRatio); i++) {
				//prng.GenerateBlock(tmp, sizeof(tmp));
				//std::string ind = Util::str2hex(std::string((const char *) tmp, ind_len));
				if(i == 0){
					for(int j = 0; j < N_entries * repeatRatio; j++){
						std::string ind = std::to_string(i);
						padd(ind,ind_len);
						//s.insert(ind);
						writer->Write(client->gen_update_request("1", keyword, ind));
					}
				}else{
					std::string ind = std::to_string(i);
						padd(ind,ind_len);
						//s.insert(ind);
						writer->Write(client->gen_update_request("1", keyword, ind));
				}
			}
			//std::cout<<s.size()<<std::endl;
			// now tell server we have finished
			writer->WritesDone();
			Status status = writer->Finish();
			std::string log = "Random DB generation: thread " + std::to_string(thread_id) + " completed: " +
							std::to_string(N_entries) + " keyword-filename";
							
			std::cout<<log<<std::endl;
			logger::log(logger::INFO) << log << std::endl;
		}

		static void random_delete_job_Euro(Client* client,std::string keyword, unsigned int thread_id, size_t N_entries){
			//std::string id_string = std::to_string(thread_id);
			CryptoPP::AutoSeededRandomPool prng;
			int ind_len = AES::BLOCKSIZE - 1 ; // AES::BLOCKSIZE = 16
			byte tmp[ind_len];
			// for gRPC
			UpdateRequestMessage request;
			ClientContext context;
			ExecuteStatus exec_status;
			std::unique_ptr <RPC::Stub> stub_(RPC::NewStub(grpc::CreateChannel("0.0.0.0:50051", grpc::InsecureChannelCredentials())));
			std::unique_ptr <ClientWriterInterface<UpdateRequestMessage>> writer(stub_->batch_update(&context, &exec_status));
			std::unordered_set<std::string> s;
			for (size_t i = 0; i < N_entries; i++) {
				prng.GenerateBlock(tmp, sizeof(tmp));
				std::string ind = /*Util.str2hex*/(std::string((const char *) tmp, ind_len));
				s.insert(ind);
				writer->Write(client->gen_update_request("0", keyword, ind));
			}
			// now tell server we have finished
			writer->WritesDone();
			std::cout<<s.size()<<std::endl;
			Status status = writer->Finish();
			std::string log = "Random DB delete: thread " + std::to_string(thread_id) + " completed: " + std::to_string(N_entries) + " keyword-filename";
			logger::log(logger::INFO) << log << std::endl;
		}

		static void batch_delete_job(Client* client,std::string keyword, unsigned int thread_id, size_t N_entries){
			//std::string id_string = std::to_string(thread_id);
			CryptoPP::AutoSeededRandomPool prng;
			int ind_len = AES::BLOCKSIZE - 1 ; // AES::BLOCKSIZE = 16
			byte tmp[ind_len];
			// for gRPC
			UpdateRequestMessage request;
			ClientContext context;
			ExecuteStatus exec_status;
			std::unique_ptr <RPC::Stub> stub_(RPC::NewStub(grpc::CreateChannel("0.0.0.0:50051", grpc::InsecureChannelCredentials())));
			std::unique_ptr <ClientWriterInterface<UpdateRequestMessage>> writer(stub_->batch_update(&context, &exec_status));
			std::unordered_set<std::string> s;
			for (size_t i = 0; i < N_entries; i++) {
				std::string ind = std::to_string(i);
				padd(ind,ind_len);
				//s.insert(ind);
				writer->Write(client->gen_update_request("0", keyword, ind));
			}
			// now tell server we have finished
			writer->WritesDone();
			std::cout<<s.size()<<std::endl;
			Status status = writer->Finish();
			std::string log = "Random DB delete: thread " + std::to_string(thread_id) + " completed: " + std::to_string(N_entries) + " keyword-filename";
			logger::log(logger::INFO) << log << std::endl;
		}
		
		void random_delete_db(Client& client, size_t N_entries, std::string keyword, unsigned int n_threads){
			logger::log(logger::INFO) << "in random_delete_db" << std::endl;
			std::vector <std::thread> threads;
			// std::mutex rpc_mutex;
			struct timeval t1, t2;
			gettimeofday(&t1, NULL);
			int numOfEntries1 = N_entries / n_threads;
			int numOfEntries2 = N_entries / n_threads + N_entries % n_threads;
			for (unsigned int i = 0; i < n_threads - 1; i++) {
				threads.push_back(std::thread(random_delete_job_Euro, &client, keyword, i, numOfEntries1));
			}
			threads.push_back(std::thread(random_delete_job_Euro, &client, keyword, n_threads - 1, numOfEntries2));
			for (unsigned int i = 0; i < n_threads; i++) {
				threads[i].join();
			}
			gettimeofday(&t2, NULL);
		}

		void batch_delete(Client& client, size_t N_entries, std::string keyword, unsigned int n_threads){
			logger::log(logger::INFO) << "in batch_delete" << std::endl;
			std::vector <std::thread> threads;
			// std::mutex rpc_mutex;
			struct timeval t1, t2;
			gettimeofday(&t1, NULL);
			int numOfEntries1 = N_entries / n_threads;
			int numOfEntries2 = N_entries / n_threads + N_entries % n_threads;
			for (unsigned int i = 0; i < n_threads - 1; i++) {
				threads.push_back(std::thread(batch_delete_job, &client, keyword, i, numOfEntries1));
			}
			threads.push_back(std::thread(batch_delete_job, &client, keyword, n_threads - 1, numOfEntries2));
			for (unsigned int i = 0; i < n_threads; i++) {
				threads[i].join();
			}
			gettimeofday(&t2, NULL);
		}

		void gen_repeat_db(Client& client, size_t N_entries, std::string keyword, unsigned int n_threads, double repeatRatio){
			logger::log(logger::INFO) << "in gen_repeat_db" << std::endl;
			std::vector <std::thread> threads;
			// std::mutex rpc_mutex;
			struct timeval t1, t2;
			gettimeofday(&t1, NULL);
			int numOfEntries1 = N_entries / n_threads;
			int numOfEntries2 = N_entries / n_threads + N_entries % n_threads;
			for (unsigned int i = 0; i < n_threads - 1; i++) {
				threads.push_back(std::thread(generation_job_Euro_repeat, &client, keyword, i, numOfEntries1, repeatRatio));
			}
			threads.push_back(std::thread(generation_job_Euro_repeat, &client,keyword, n_threads - 1, numOfEntries2, repeatRatio));
			for (unsigned int i = 0; i < n_threads; i++) {
				threads[i].join();
			}
			gettimeofday(&t2, NULL);
			//输出到日志文件
			logger::log_benchmark()<< "keyword: "+keyword+" "+std::to_string(N_entries)+" entries "+"update time: "
								<< ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000.0 << " ms"
								<< std::endl;
			//输出到终端
			logger::log(logger::INFO)<< "keyword: "+keyword+" "+std::to_string(N_entries)+" entries "+"update time: "
									<< ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) / 1000.0 << " ms"
									<< std::endl;
		}

		static void gen_rdb(std::string db_path, size_t N_entries)
         {
			
			rocksdb::DB* ss_db;

			rocksdb::Options options;
    		options.create_if_missing = true;
	    	Util::set_db_common_options(options);

			rocksdb::Status s = rocksdb::DB::Open(options, db_path, &ss_db);

			if(!s.ok()) {
				std::cerr<< "In gen_rdb_nrpc(), open db error: "<< s.ToString() <<std::endl;	
			}

			int c = 0;

			AutoSeededRandomPool prng;
			int ind_len = AES::BLOCKSIZE; // AES::BLOCKSIZE = 16
			byte tmp[ind_len];


         	for(int i = 0; i < N_entries; i++) {
				prng.GenerateBlock(tmp, sizeof(tmp));
				std::string key = (std::string((const char*)tmp, ind_len));
				prng.GenerateBlock(tmp, sizeof(tmp));
				std::string value = (std::string((const char*)tmp, ind_len/2));
				s = ss_db->Put(rocksdb::WriteOptions(), key, value);
				c++;
				if ( c % 100000 == 0 ) logger::log(logger::INFO) << "RDB generation: " << ": " << c << " entries generated\r" << std::flush;
			}

		 }// gen_rdb
		 
	// 	void eval_trace(Client &client, int thread_num){ 
	// 		// for trace
	// 	   double search_rate[3] = {0.0001, 0.001, 0.01};
	// 	   const std::string TraceKeywordGroupBase = "Trace";
	// 	   int last_uc; 

	// 	   for(int i = 0; i < thread_num; i++) 
	// 		   for(int j = 0; j < 3; j++)
	// 		   {
	// 			   std::string w = TraceKeywordGroupBase + "_" + std::to_string(i) + "_" + std::to_string(j) + "_5";
	// 			   last_uc = 0;
	// 			   for(int c = 1; c <= 1e5; c++) 
	// 			   {
	// 				   double r = rand_0_to_1(c);
	// 				   bool is_search = sample(r, search_rate[j]);
	// 				   if(is_search) {
	// 					   // last_uc = c;
	// 					   client.search_for_trace( w, c -last_uc );
	// 					   last_uc = c;
	// 					   search_log(w, c);
	// 				   }
	// 			   }
	// 		   }
	//    }

} //namespace DistSSE
