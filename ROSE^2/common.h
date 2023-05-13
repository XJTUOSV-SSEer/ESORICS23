#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <string>

//IV_size + cipher_size
#define CIPHER_SIZE (16 + 16)
#define OMAP_SIZE 1000000

//int N = 131072; //2^17

enum OpType
{
    op_del = 0,
    op_add = 1,
    op_srh = 2
};

//Rose2
struct ST_value{
    std::string K_u;
    std::string delta_k;
    int cnt_i;
    int cnt_d;
    int sn;
};

struct I_n{
    int Node;
    int Num;
    int lNode;
    int lNum;
    int rNode;
    int rNum;
};

struct search_token{
    std::string K_u;
    std::string delta_k;
    std::string tk;
    int sn;
    int cnt_d;
    int cnt_i;
};

//out:32位
int PRF_F(unsigned char *out, const unsigned char *key, const std::string &keyword);

//out:32位
int PRF_F(unsigned char *out, const unsigned char *key, const std::string &keyword, const int id, OpType op);

int Hash_H(unsigned char *out, int out_len, const unsigned char *in1, const unsigned char *R);

int Hash_G(unsigned char *out, const unsigned char *data, const unsigned char *R);

int print_hex(unsigned char *data, int len);

int Xor(int _bytes, const unsigned char *in1, const unsigned char *in2, unsigned char *out);

void save_string(FILE*f_out, const std::string & str);

std::string load_string(FILE *f_in);


#endif
