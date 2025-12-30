#ifndef BLOOM_H
#define BLOOM_H

#include <vector>
#include "Hash.hpp"
#include "tool.hpp"

class Bloom {
private:
    int k;
    long bits;
    long seed;
    int arraySize;
    std::vector<long> data;

    static int getBestK(double bitsPerKey);

public:
    Bloom(int entryCount, double bitsPerKey, int k_);
    void add(long key);
    [[nodiscard]] bool mayContain(long key) const;
    [[nodiscard]] auto getData() const -> std::vector<std::vector<unsigned char>>;
    static auto construct(const std::vector<long>& keys, double bitsPerKey) -> Bloom;
};

#endif