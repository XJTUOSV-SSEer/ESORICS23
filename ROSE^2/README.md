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
```

## update

```bash
# path of database, flag, keyword, id, path of logfile
./rpc_client ./database/my.cdb 2 keyword 200 benchmark_client_rose_update.out 
./rpc_client ./database/my.cdb 4 keyword 200 benchmark_client_rose_update.out
```

## search
```bash
./rpc_client ./database/my.cdb 3 secur benchmark_client_rose_search.out
```
