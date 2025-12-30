#include "Hash.hpp"

void Hash::setSeed(uint64_t seed) {
    rng.seed(seed);
}

long Hash::hash64(long x, long seed) { // 使用 long 类型，与声明一致
    x += seed;
    x = (x ^ (x >> 33)) * 0xff51afd7ed558ccdL;
    x = (x ^ (x >> 33)) * 0xc4ceb9fe1a85ec53L;
    return x ^ (x >> 33);
}


// 移除 reduce 的定义，因为已经在 Hash.hpp 中内联实现
// int Hash::reduce(int hash, int n) {
//     return static_cast<int>((static_cast<uint64_t>(hash & 0xffffffff) * n) >> 32);
// }

std::vector<unsigned char> Hash::Get_SHA_256(std::string_view input) { // 名称改为 Get_SHA_256，与声明一致
    std::vector<unsigned char> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash.data());
    return hash;
}

std::vector<unsigned char> Hash::Get_SHA_128(std::string_view input) { // 名称改为 Get_SHA_128，与声明一致
    auto hash = Get_SHA_256(input);
    hash.resize(16);
    return hash;
}