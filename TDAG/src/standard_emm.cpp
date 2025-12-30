#include "standard_emm.hpp"
#include "Hash.hpp"      // Assuming you have a Hash utility
#include "AESUtil.hpp"  // Assuming you have an AES utility
#include <iostream>
#include <string_view>

StandardEMM::StandardEMM(const std::string& k_token, const std::string& k_enc) 
    : k_token_(k_token), k_enc_(k_enc) {
    // Pre-calculate the encryption key hash to avoid re-computation
    enc_key_hash_ = Hash::Get_SHA_256(k_enc_);
}

void StandardEMM::buildEMM(const KeywordMap& plaintext_mm) {
    tset_.clear();

    for (const auto& [keyword, record_ids] : plaintext_mm) {
        if (record_ids.empty()) continue;
         
        // 1. 生成关键词的基础令牌
        std::string token_input = k_token_ + keyword;
        auto token = Hash::Get_SHA_256(token_input);
        
        // 2. 对每个记录ID进行加密和存储
        for (size_t i = 0; i < record_ids.size(); ++i) {
            std::vector<unsigned char> token_with_index = token;
            std::string index_str = std::to_string(i);
            token_with_index.insert(token_with_index.end(), index_str.begin(), index_str.end());
            
            auto ct_label = Hash::Get_SHA_256({reinterpret_cast<const char*>(token_with_index.data()), token_with_index.size()});

            std::string record_str = std::to_string(record_ids[i]);
            auto ct_value = AESUtil::encrypt(enc_key_hash_, record_str);

            // In this basic scheme, each encrypted value gets a unique label
            // To handle multiple values per label, the map's value would be a vector
            tset_[ct_label] = {ct_value}; 
        }  
    }
}

std::vector<Ciphertext> StandardEMM::generateTokens(const Label& keyword) const {
    // This EMM scheme generates a single base token per keyword.
    // The search logic will append indices to it.
    std::string token_input = k_token_ + keyword;
    return { Hash::Get_SHA_256(token_input) };
}

std::vector<Ciphertext> StandardEMM::generateTokens(const std::vector<std::string>& keywords) const {
    std::vector<Ciphertext> tokens;
    tokens.reserve(keywords.size()); // 预分配空间以提高效率
    for (const auto& keyword : keywords) {
        std::string token_input = k_token_ + keyword;
        tokens.push_back(Hash::Get_SHA_256(token_input));
    }
    return tokens;
}

EncryptedResult StandardEMM::query(const std::vector<Ciphertext>& tokens) const {
    EncryptedResult results;
    if (tokens.empty()) return results;

    // A token in this scheme is the base hash for a keyword.
    // The search iterates by appending indices to find all associated values.
    for (const auto& base_token : tokens) {
        size_t index = 0;
        while (true) {
            std::vector<unsigned char> token_with_index = base_token;
            std::string index_str = std::to_string(index);
            token_with_index.insert(token_with_index.end(), index_str.begin(), index_str.end());
            
            auto ct_label = Hash::Get_SHA_256({reinterpret_cast<const char*>(token_with_index.data()), token_with_index.size()});
            
            auto it = tset_.find(ct_label);
            if (it != tset_.end()) {
                results[ct_label] = it->second; // 返回整个向量
            } else {
                // No more records for this keyword, break inner loop
                break;
            }
            ++index;
        }
    }
    return results;
}

size_t StandardEMM::getStorageSize() const {
    size_t total_size = 0;
    //遍历加密索引的所有标签和值，计算总字节数
    for (const auto& [label, records] : tset_) {
        //// 累加键 (ct_label) 的大小
        total_size += label.size();
        // // 累加值 (vector<Ciphertext>) 中每个加密记录的大小
        for (const auto& record : records) {
            total_size += record.size();
        }
    }
    return total_size;
}

std::vector<RecordID> StandardEMM::decryptResults(const EncryptedResult& encrypted_results) const {
    std::vector<RecordID> decrypted_ids;
    decrypted_ids.reserve(encrypted_results.size());
    
    for (const auto& [ct_label, ct_values] : encrypted_results) {
        for (const auto& ct_value : ct_values) {
            try {
                auto decrypted_value = AESUtil::decrypt(enc_key_hash_, ct_value);
                if (decrypted_value) {
                    decrypted_ids.push_back(std::stoi(*decrypted_value));
                }
            } catch (const std::exception& e) {
                std::cerr << "Decryption or conversion failed: " << e.what() << std::endl;
            }
        }
    }
    return decrypted_ids;
}
