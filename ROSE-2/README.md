# rose-grpc

# build

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
./rpc_client ./database/my.cdb 7 keyword 3000 1 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 7 keyword 300 1 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 7 keyword 30 1 benchmark_client_rose_update.out
./rpc_client ./database/my.cdb 7 keyword 3 1 benchmark_client_rose_update.out

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