#ifndef HASH_H
#define HASH_H

#include <vector>
#include <string_view>
#include <random>
#include <openssl/sha.h>

class Hash {
private:
    inline static std::mt19937_64 rng{std::random_device{}()}; // 名称改为 rng，与 Hash.cpp 一致

public:
    static void setSeed(uint64_t seed); // 添加 setSeed 方法声明
    static std::vector<unsigned char> Get_SHA_256(std::string_view input);
    static std::vector<unsigned char> Get_SHA_128(std::string_view input);
    static long hash64(long x, long seed); // 使用 long 类型，与声明一致
    static int reduce(int hash, int n) { // 内联实现
        return static_cast<int>((static_cast<uint64_t>(hash & 0xffffffff) * n) >> 32);
    }
    static long randomSeed() { return rng(); } // 使用 long 类型，与声明一致
};

#endif