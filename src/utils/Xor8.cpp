#include "Xor8.hpp"
#include <stdexcept>
#include <algorithm>

Xor8::Xor8(const std::vector<long>& keys, const std::vector<std::vector<unsigned char>>& ct) : size{static_cast<int>(keys.size())} {
    arrayLength = getArrayLength(size);
    blockLength = arrayLength / HASHES;
    ciphertext.resize(arrayLength);

    std::vector<long> reverseOrder(arrayLength);
    std::vector<unsigned char> reverseH(arrayLength);
    int reverseOrderPos;

    do {
        seed = Hash::randomSeed();
        std::vector<unsigned char> t2count(arrayLength, 0);
        std::vector<long> t2(arrayLength, 0);

        for (int i = 0; i < size; ++i) {
            for (int hi = 0; hi < HASHES; ++hi) {
                int h = getHash(keys[i], seed, hi);
                t2[h] ^= i;
                if (++t2count[h] > 120) throw std::runtime_error("Hash collision detected");
            }
        }

        reverseOrderPos = 0;
        std::vector<std::vector<int>> alone(HASHES, std::vector<int>(blockLength));
        std::vector<int> alonePos(HASHES, 0);

        for (int nextAlone = 0; nextAlone < HASHES; ++nextAlone) {
            for (int i = 0; i < blockLength; ++i) {
                if (t2count[nextAlone * blockLength + i] == 1) {
                    alone[nextAlone][alonePos[nextAlone]++] = nextAlone * blockLength + i;
                }
            }
        }

        int found = -1;
        while (true) {
            int i = -1;
            for (int hi = 0; hi < HASHES; ++hi) {
                if (alonePos[hi] > 0) {
                    i = alone[hi][--alonePos[hi]];
                    found = hi;
                    break;
                }
            }
            if (i == -1) break;
            if (t2count[i] <= 0) continue;

            long k = t2[i];
            if (--t2count[i] != 0) throw std::logic_error("Unexpected count");

            for (int hi = 0; hi < HASHES; ++hi) {
                if (hi != found) {
                    int h = getHash(keys[k], seed, hi);
                    int newCount = --t2count[h];
                    if (newCount == 1) alone[hi][alonePos[hi]++] = h;
                    t2[h] ^= k;
                }
            }
            reverseOrder[reverseOrderPos] = k;
            reverseH[reverseOrderPos] = static_cast<unsigned char>(found);
            ++reverseOrderPos;
        }
    } while (reverseOrderPos != size);

    for (int i = reverseOrderPos - 1; i >= 0; --i) {
        int k = static_cast<int>(reverseOrder[i]);
        int found = reverseH[i];
        int change = -1;
        auto result = ct[k]; // 将变量名从 xor 改为 result
        for (int hi = 0; hi < HASHES; ++hi) {
            int h = getHash(keys[k], seed, hi);
            if (found == hi) {
                change = h;
            } else {
                if (ciphertext[h].empty()) {
                    std::vector<unsigned char> random(4);
                    std::generate(random.begin(), random.end(), []() { return rand() % 256; });
                    ciphertext[h] = Hash::Get_SHA_256(std::string_view{reinterpret_cast<char*>(random.data()), random.size()});
                }
                result = tool::Xor(result, ciphertext[h]); // 使用新变量名 result
            }
        }
        ciphertext[change] = result; // 使用新变量名 result
    }
    for (auto& c : ciphertext) {
        if (c.empty()) {
            std::vector<unsigned char> random(4);
            std::generate(random.begin(), random.end(), []() { return rand() % 256; });
            c = Hash::Get_SHA_256(std::string_view{reinterpret_cast<char*>(random.data()), random.size()});
        }
    }
}

int Xor8::getHash(long key, long seed, int index) const {
    long r = rotl(Hash::hash64(key, seed), 21 * index);
    r = Hash::reduce(static_cast<int>(r), blockLength);
    return static_cast<int>(r + index * blockLength);
}

int Xor8::fingerprint(long hash) const {
    return static_cast<int>(hash & ((1 << BITS_PER_FINGERPRINT) - 1));
}

auto Xor8::search(long key) const -> std::vector<unsigned char> {
    long hash = Hash::hash64(key, seed);
    int r[3] = {static_cast<int>(hash), static_cast<int>(rotl(hash, 21)), static_cast<int>(rotl(hash, 42))};
    int h[3];
    for (int i = 0; i < 3; ++i) {
        h[i] = Hash::reduce(r[i], blockLength) + i * blockLength;
    }
    return tool::Xor(ciphertext[h[0]], tool::Xor(ciphertext[h[1]], ciphertext[h[2]]));
}