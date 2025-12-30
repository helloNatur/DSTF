#ifndef TOOL_H
#define TOOL_H

#include <vector>
#include <string_view>

class tool {
public:
    static long bytesToLong(const std::vector<unsigned char>& bytes);
    static auto longToBytes(long num) -> std::vector<unsigned char>;
    static bool Xor_Empty(const std::vector<unsigned char>& xor_value);
    static auto Xor(const std::vector<unsigned char>& x, const std::vector<unsigned char>& y) -> std::vector<unsigned char>;
};

#endif