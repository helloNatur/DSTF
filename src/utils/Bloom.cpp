#include "Bloom.hpp"

Bloom::Bloom(int entryCount, double bitsPerKey, int k_)
    : k{k_}, bits{static_cast<long>(entryCount * bitsPerKey)}, seed{Hash::randomSeed()}, arraySize{static_cast<int>(bits / 64 + 1)} {
    data.resize(arraySize);
}

int Bloom::getBestK(double bitsPerKey) {
    return static_cast<int>(bitsPerKey * 0.7);
}

Bloom Bloom::construct(const std::vector<long>& keys, double bitsPerKey) { // 将 uint64_t 改为 long，与声明一致
    int k = getBestK(bitsPerKey);
    Bloom f(static_cast<int>(keys.size()), bitsPerKey, k);
    for (long x : keys) f.add(x);
    return f;
}

void Bloom::add(long key) { // 将 uint64_t 改为 long，与声明一致
    for (int i = 0; i < k; ++i) {
        long hash = Hash::hash64(key, seed + i);
        int index = Hash::reduce(static_cast<int>(hash), arraySize);
        int pos = Hash::reduce(static_cast<int>(hash >> 32), 64);
        data[index] |= (1L << pos);
    }
}

bool Bloom::mayContain(long key) const { // 将 uint64_t 改为 long，与声明一致
    for (int i = 0; i < k; ++i) {
        long hash = Hash::hash64(key, seed + i);
        int index = Hash::reduce(static_cast<int>(hash), arraySize);
        int pos = Hash::reduce(static_cast<int>(hash >> 32), 64);
        if ((data[index] & (1L << pos)) == 0) {
            return false;
        }
    }
    return true;
}

std::vector<std::vector<unsigned char>> Bloom::getData() const {
    std::vector<std::vector<unsigned char>> result;
    result.reserve(data.size());
    for (long val : data) {
        result.push_back(tool::longToBytes(val));
    }
    return result;
}