# rose-grpc

# build

```bash
$ make clean
$ make
```

# gen_db
## rose-grpc中的gendb：从文件中读到rocksdb（持久化存储，方便search）

## server side
 
```bash
# path of database, path of cache dadabase, number of threads, path of output log
$ ./rpc_server ./database/my.sdb ./database/my.scadb 1 benchmark_server.out
```

## client side
```bash
# gen_db
## rose-grpc中的gendb：从文件中读到rocksdb（持久化存储，方便search）
## path of database, flag, number of threads, path of logfile
./rpc_client ./database/my.cdb 1 1 benchmark_client_rose_gen_db.out
./rpc_client ./database/my.cdb 1 1 keyword 10 benchmark_client_rose_gen_db.out

#update_add
## path of database, flag, number of threads, number of entries, path of logfile
./rpc_client ./database/my.cdb 2 1 2001 1000 benchmark_client_rose.update_add.out

#search
## path of database, flag, number of threads, keyword, path of logfile
./rpc_client ./database/my.cdb 3 1 keyword benchmark_client_rose.search.out   #还不能搜不存在的

#update_delete
## path of database, flag, number of threads, number of entries, path of logfile
./rpc_client ./database/my.cdb 4 1 2001 1000 benchmark_client_rose.update_del.out
```

# 不能连续搜索 
# 删除会报错double free or corruption （已经解决）
# 不能搜索不存在的
# 先增加后删除再搜索会报段错误 (有概率会)


# 建立数据库后直接进行删除是可以的


# functions test

this part is to prove the code is right and completely implement our scheme



## client side
```bash
# insert one keyword-filename pair
./rpc_client /tmp/my.cdb 1

# insert three keyword-docuemnt pairs
./rpc_client /tmp/my.cdb 2

# search `keyword`
./rpc_client /tmp/my.cdb 3

# search and verify `keyword`
./rpc_client /tmp/my.cdb 4
```

# benchmark

## server

```bash
# path of database, path of cache dadabase, number of threads, path of output log
./rpc_server /tmp/my.sdb /tmp/my.scadb 1 benchmark_server.out
```
## Update
```bash
# path of database, number of entries, keyword, flag, number of threads, path of logfile
./rpc_client /tmp/my.cdb 10 keyword_10e1 2 1 benchmark_client_10e1.update.out
./rpc_client /tmp/my.cdb 100 keyword_10e2 2 1 benchmark_client_10e2.update.out
./rpc_client /tmp/my.cdb 1000 keyword_10e3 2 1 benchmark_client_10e3.update.out
./rpc_client /tmp/my.cdb 10000 keyword_10e4 2 1 benchmark_client_10e4.update.out
./rpc_client /tmp/my.cdb 100000 keyword_10e5 2 1 benchmark_client_10e5.update.out

# rose
./rpc_client /tmp/my.cdb 


./rpc_client /tmp/my.cdb 10 yang 2 1 benchmark_client_yang.update.out
```
## Search
```bash
# path of database, number of entries, keyword, flag, number of threads, path of logfile
./rpc_client /tmp/my.cdb 4 keyword_10e1 3 1 benchmark_client_10e1.search.out
./rpc_client /tmp/my.cdb 4 keyword_10e2 3 1 benchmark_client_10e2.search.out
./rpc_client /tmp/my.cdb 4 keyword_10e3 3 1 benchmark_client_10e3.search.out
./rpc_client /tmp/my.cdb 4 keyword_10e4 3 1 benchmark_client_10e4.search.out
./rpc_client /tmp/my.cdb 4 keyword_10e5 3 1 benchmark_client_10e5.search.out

./rpc_client /tmp/my.cdb 4 yang 3 1 benchmark_client_yang.search.out
```

## Verify
```bash
# path of database, number of entries, keyword, flag, number of threads, path of logfile
./rpc_client /tmp/my.cdb 10 keyword_10e1 4 1 benchmark_client_10e1.verify.out
./rpc_client /tmp/my.cdb 100 keyword_10e2 4 1 benchmak_client_10e2.verify.out
./rpc_client /tmp/my.cdb 1000 keyword_10e3 4 1 benchmark_client_10e3.verify.out
./rpc_client /tmp/my.cdb 10000 keyword_10e4 4 1 benchmark_client_10e4.verify.out
./rpc_client /tmp/my.cdb 100000 keyword_10e5 4 1 benchmark_client_10e5.verify.out
```

# core code

```cplusplus
gen_update_token(){
    sc = get_search_time(w);
    tw = gen_enc_token(w);
    uc = get_update_time(w);
    l = Util::H1(tw + std::to_string(uc + 1));
    e = Util::Xor(op + ind, Util::H2(tw + std::to_string(uc + 1)));
    set_update_time(w, uc + 1);
    // search_time is used to store the proof
    set_search_time(w, Xor(sc, ind));
}

gen_search_token(){
    tw = gen_enc_token(w);
    uc = get_update_time(w);
}

search() {
    for(i=uc to 1){
        u = H1(tw + uc);
        e = get(u);
        (op, ind) = Xor(e, u);
    }
}
```

# use scripts

## Update

```bash
sh ./scripts/clear.sh
sh ./scripts/runserver.sh
sh ./scripts/update_batch.sh
# see output
sh ./scripts/update_out.sh
```

## Search

```bash
sh ./scripts/clear.sh
sh ./scripts/runserver.sh
sh ./scripts/search_batch.sh
# see output
sh ./scripts/search_out.sh
```

## Verify

```bash
sh ./scripts/clear.sh
sh ./scripts/runserver.sh
sh ./scripts/verify_batch.sh
# see output
sh ./scripts/verify_out.sh
```