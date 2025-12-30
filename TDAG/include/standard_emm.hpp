#pragma once

#include "emm_interface.hpp"
#include <string>


/**
 * @class StandardEMM
 * @brief A basic implementation of a keyword-based Searchable Symmetric Encryption scheme.
 *
 * This class implements the I_EMM interface using a standard encrypted inverted
 * index construction based on HMAC and AES.
 */
class StandardEMM : public EMM_Interface {
private:
    EncryptedIndex tset_;
    std::string k_token_;
    std::string k_enc_;
    Ciphertext enc_key_hash_; // Pre-hashed key for encryption/decryption

public:
    StandardEMM(const std::string& k_token, const std::string& k_enc);
    
    void buildEMM(const KeywordMap& plaintext_mm) override;
    std::vector<Ciphertext> generateTokens(const Label& keyword) const override;
    // 额外的多关键字版本（非虚函数）
    std::vector<Ciphertext> generateTokens(const std::vector<std::string>& keywords) const;
    EncryptedResult query(const std::vector<Ciphertext>& tokens) const override;
    std::vector<RecordID> decryptResults(const EncryptedResult& enc_results) const override;
    
    const EncryptedIndex& getEncryptedIndex() const override ;
    size_t getStorageSize() const override;

};

inline const EncryptedIndex& StandardEMM::getEncryptedIndex() const {
    return tset_;
}