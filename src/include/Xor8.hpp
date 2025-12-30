#ifndef XOR8_H
#define XOR8_H

#include <vector>
#include "Hash.hpp"
#include "tool.hpp"

// 自定义 rotl 函数，用于循环左移
inline long rotl(long x, int k) {
    return (x << k) | (x >> (64 - k));
}

class Xor8 {
private:
    static constexpr int BITS_PER_FINGERPRINT = 8;
    static constexpr int HASHES = 3;
    static constexpr int OFFSET = 2;
    static constexpr int FACTOR_TIMES_100 = 123;

    int size;
    int arrayLength;
    int blockLength;
    long seed;
    std::vector<std::vector<unsigned char>> ciphertext;

    static int getArrayLength(int size) {
        return OFFSET + (FACTOR_TIMES_100 * size) / 100;
    }
    int getHash(long key, long seed, int index) const;
    int fingerprint(long hash) const;

public:
    Xor8(const std::vector<long>& keys, const std::vector<std::vector<unsigned char>>& ct);
    [[nodiscard]] auto search(long key) const -> std::vector<unsigned char>;
    [[nodiscard]] auto getCiphertext() const -> std::vector<std::vector<unsigned char>> { return ciphertext; }
};

#endif