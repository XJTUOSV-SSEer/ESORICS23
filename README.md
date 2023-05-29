# File Structure

```
Rose^2/
├── README.md                                       //  introduction
├── database                                        //  database file for rocksdb
├── dataset                                         //  dataset for testing
├── OMAP                                            //  OMAP
│   ├── AES.cpp
│   ├── AES.hpp
│   ├── AVLTree.cpp
│   ├── AVLTree.h
│   ├── Bid.cpp
│   ├── Bid.h
│   ├── OMAP.cpp
│   ├── OMAP.h
│   ├── ORAM.cpp
│   ├── ORAM.hpp
│   ├── RAMStore.cpp
│   ├── RAMStore.cpp
│   ├── Types.hpp
│   ├── Utilities.cpp
│   ├── RAMStore.cpp
│   └── Utilities.h
├── common.h                                        //  some parameters and strcut for the scheme
├── common.cpp                                    
├── DistSSE.client.h                                //  client class
├── DistSSE.db_generator.h                          //  generate database
├── DistSSE.server.h                                //  server class
├── DistSSE.Util.cc
├── DistSSE.Util.h                                  //  tools
├── KUPRF.cpp
├── KUPRF.h                                         //  key updatable PRF
├── logger.h                                        //  tools for log
├── logger.cc
├── rpc_server.cc                                   //  main for runserver
├── rpc_client.cc                                   //  main for runclient
└── makefile
```
# Datasets

Email-enron: https://www.cs.cmu.edu/./enron/

# Prepare Environment

C++11, Crypto++, RocksDB and gRPC, relic, openssl 

# Building Procedure

```bash
$ make clean
$ make
```
# op

## boot server
```bash
# path of database, path of cache dadabase, number of threads, path of output log
./rpc_server ./database/my.sdb ./database/my.scadb 1 benchmark_server.out
```

## gen_db

```bash
# path of database, flag, number of threads, path of logfile
./rpc_client ./database/my.cdb 1 1 benchmark_client_rose_gen_db.out
./rpc_client ./database/my.cdb 5 keyword 10 1 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 5 keyword 100 1 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 5 keyword 1000 1 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 5 keyword 10000 1 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 5 keyword 100000 1 benchmark_client_rose_gen_random_db.out

# gen_repeat_db
./rpc_client ./database/my.cdb 8 keyword 10000 1 10 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 8 keyword 10000 1 30 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 8 keyword 10000 1 50 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 8 keyword 10000 1 70 benchmark_client_rose_gen_random_db.out
./rpc_client ./database/my.cdb 8 keyword 10000 1 90 benchmark_client_rose_gen_random_db.out

```

## update

```bash
# path of database, flag, keyword, id, path of logfile
# add
./rpc_client ./database/my.cdb 2 keyword 200 benchmark_client_rose_update.out 
# del
./rpc_client ./database/my.cdb 4 secur 3 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 4 secur 10 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 4 secur 31 benchmark_client_rose_update.out
```

## batchUpdate
```bash
# path of database, flag, keyword, N_entries, path of logfile
# batch add
./rpc_client ./database/my.cdb 6 keyword 100 1 benchmark_client_rose_update.out 
# batch del
./rpc_client ./database/my.cdb 7 keyword 1000 1 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 7 keyword 3000 1 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 7 keyword 5000 1 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 7 keyword 7000 1 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 7 keyword 9000 1 benchmark_client_rose_update.out

# batch useless del
./rpc_client ./database/my.cdb 9 keyword 10000 1 10 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 9 keyword 10000 1 30 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 9 keyword 10000 1 50 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 9 keyword 10000 1 70 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 9 keyword 10000 1 90 benchmark_client_rose_update.out
```

## search
```bash
./rpc_client ./database/my.cdb 3 keyword benchmark_client_rose_search.out
```