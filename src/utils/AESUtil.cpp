#include "AESUtil.hpp"
#include <openssl/evp.h>
#include <stdexcept>

auto AESUtil::encrypt(const std::vector<unsigned char>& key, std::string_view plaintext) -> std::vector<unsigned char> {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

    std::vector<unsigned char> iv{InitVector.begin(), InitVector.end()};
    std::vector<unsigned char> ciphertext(plaintext.size() + AES_BLOCK_SIZE);

    int len, ciphertext_len;
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()))
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &len, reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size()))
        throw std::runtime_error("EVP_EncryptUpdate failed");
    ciphertext_len = len;
    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len))
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    ciphertext_len += len;

    ciphertext.resize(ciphertext_len);
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

auto AESUtil::decrypt(const std::vector<unsigned char>& key, const std::vector<unsigned char>& ciphertext) -> std::optional<std::string> {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::nullopt;

    std::vector<unsigned char> iv{InitVector.begin(), InitVector.end()};
    std::vector<unsigned char> plaintext(ciphertext.size());

    int len, plaintext_len;
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    plaintext_len = len;
    if (1 != EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    plaintext_len += len;

    plaintext.resize(plaintext_len);
    EVP_CIPHER_CTX_free(ctx);
    return std::string{plaintext.begin(), plaintext.end()};
}