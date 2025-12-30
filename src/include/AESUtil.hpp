#ifndef AESUTIL_H
#define AESUTIL_H

#include <vector>
#include <string_view>
#include <optional>
#include <openssl/aes.h>

class AESUtil {
private:
    inline static const std::string_view InitVector = "EncDecInitVector";

public:
    static auto encrypt(const std::vector<unsigned char>& key, std::string_view plaintext) -> std::vector<unsigned char>;
    static auto decrypt(const std::vector<unsigned char>& key, const std::vector<unsigned char>& ciphertext) -> std::optional<std::string>;
};

#endif