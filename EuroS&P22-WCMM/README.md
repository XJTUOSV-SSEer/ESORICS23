# FAST/FASTIO
Implementation of forward private SSE scheme, including FAST and FASTIO (see https://arxiv.org/abs/1710.00183)

# Pre-requisites
C++11, Crypto++, RocksDB and gRPC

## Installing gRPC
Install gRPC's C++ binding (see [here](https://github.com/grpc/grpc/tree/release-0_14/src/cpp) for the 0.14 release).

## Installing RocksDB
Rocksdb 5.7 release. See the [installation guide](https://github.com/facebook/rocksdb/blob/master/INSTALL.md).

## Getting the code
```sh
 $ git clone -b 2.0 https://github.com/BintaSong/DistSSE
```
please check into 2.0 version for building

# Building
```sh
 $ make
```
# run
./rpc_server ./database/my.sdb ./database/my.scadb 1 benchmark_server.out

# update
./rpc_client ./database/my.cdb 100000 keyword_10e1 2 1 benchmark_client_10e1.update.out

# search
./rpc_client ./database/my.cdb 4 keyword_10e1 3 1 benchmark_client_10e1.search.out

# random delete
./rpc_client ./database/my.cdb 10 keyword_10e1 4 1 benchmark_random_delete.out


# 待解决的bug:
server端Reproof没运行
## ReProof后再进行一次搜索server会段错误 √
## ReProof后再Update，再搜索结果不对，表现为ReProof压缩的结果列表内容不对 √