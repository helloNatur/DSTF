#include "tool.hpp"
#include <algorithm>

long tool::bytesToLong(const std::vector<unsigned char>& bytes) {
    long num = 0;
    size_t len = std::min(bytes.size(), size_t{8});
    for (size_t i = 0; i < len; ++i) {
        num = (num << 8) | (bytes[len - i - 1] & 0xff);
    }
    return num;
}

auto tool::longToBytes(long num) -> std::vector<unsigned char> {
    std::vector<unsigned char> byteNum(8);
    for (int i = 0; i < 8; ++i) {
        int offset = 64 - (i + 1) * 8;
        byteNum[i] = static_cast<unsigned char>((num >> offset) & 0xff);
    }
    return byteNum;
}

bool tool::Xor_Empty(const std::vector<unsigned char>& xor_value) {
    return std::all_of(xor_value.begin(), xor_value.end(), [](unsigned char b) { return b == 0; });
}

auto tool::Xor(const std::vector<unsigned char>& x, const std::vector<unsigned char>& y) -> std::vector<unsigned char> {
    size_t min = std::min(x.size(), y.size());
    std::vector<unsigned char> temp(min);
    for (size_t i = 0; i < min; ++i) {
        temp[i] = x[i] ^ y[i];
    }
    return temp;
}