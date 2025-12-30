// EMM_Interface.cpp
#include "emm_interface.hpp"
#include "standard_emm.hpp"
#include "Hash.hpp"
#include "AESUtil.hpp"
#include <iostream>

void StandardEMM::buildEMM(const std::unordered_map<std::string, std::vector<int>>& plaintext_mm) {
    tset_.clear();
    

    for (const auto& [keyword, record_ids] : plaintext_mm) {
        if (record_ids.empty()) continue;
        
        // 生成关键词token - 复用JXT+的哈希方式
        std::string token_input = k_token_ + keyword;
        auto token = Hash::Get_SHA_256(token_input);
        
        // 加密记录ID列表
        for (size_t index=0;index<record_ids.size();++index) {
            std::vector<unsigned char> token_index = token;
            std::string index_str = std::to_string(index);
            token_index.insert(token_index.end(), index_str.begin(), index_str.end());
            // 修复：使用 string_view 避免 NUL 问题
            auto ct_label = Hash::Get_SHA_256(std::string_view(reinterpret_cast<const char*>(token_index.data()), token_index.size()));

            std::string record_str = std::to_string(record_ids[index]);
            
            auto key_vec = Hash::Get_SHA_256(k_enc_);// 修改：使用 SHA256 生成 32 字节密钥

            auto ct_value = AESUtil::encrypt(key_vec, record_str);
            tset_[ct_label] = {ct_value};
        }  
    }
    
    std::cout << "EMM built with " << tset_.size() << " entries" << std::endl;
}

// 单关键字版本
std::vector<std::vector<unsigned char>> StandardEMM::generateTokens(const std::string& keyword) const {
    std::string token_input = k_token_ + keyword;
    std::vector<std::vector<unsigned char>> tokens;
    tokens.push_back(Hash::Get_SHA_256(token_input));
    return tokens;
}

// 新增：多关键字版本
std::vector<std::vector<unsigned char>> StandardEMM::generateTokens(const std::vector<std::string>& keywords) const {
    std::vector<std::vector<unsigned char>> tokens;
    tokens.reserve(keywords.size()); // 预分配空间以提高效率
    for (const auto& keyword : keywords) {
        std::string token_input = k_token_ + keyword;
        tokens.push_back(Hash::Get_SHA_256(token_input));
    }
    return tokens;
}

std::unordered_map<std::vector<unsigned char>, std::vector<unsigned char>, VectorHash> 
StandardEMM::query(const std::vector<std::vector<unsigned char>>& tokens) const {
    std::unordered_map<std::vector<unsigned char>, std::vector<unsigned char>, VectorHash> results;
    if(tokens.empty()) return results;

    // --- 核心修改：遍历所有传入的token ---
    for(const auto&token : tokens){
        size_t index = 0;
        while (true){
            std::vector<unsigned char> token_index = token;
            std::string index_str = std::to_string(index);
            token_index.insert(token_index.end(), index_str.begin(), index_str.end());
            auto ct_label = Hash::Get_SHA_256(std::string_view(reinterpret_cast<const char*>(token_index.data()), token_index.size()));
            
            auto it = tset_.find(ct_label);
            if (it != tset_.end()){
                for(const auto&record :it->second){
                    results[ct_label] = record;
                }
            }else{
                break;
            }
            ++index;
        }
    }
    return results;
}

size_t StandardEMM::getStorageSize() const {
    size_t total_size = 0;
    for (const auto& [token, records] : tset_) {
        total_size += token.size();
        for (const auto& record : records) {
            total_size += record.size();
        }
    }
    return total_size;
}

std::vector<int> StandardEMM::decryptResults(
    const std::unordered_map<std::vector<unsigned char>, 
    std::vector<unsigned char>, VectorHash>& encrypted_results) const {
    
    std::vector<int> decrypted_results;
    auto key_vec = Hash::Get_SHA_256(k_enc_);
    
    
    for (const auto& [ct_label, ct_value] : encrypted_results) {
        try {
            auto decrypted_value = AESUtil::decrypt(key_vec, ct_value);
            if(decrypted_value){
                decrypted_results.push_back(std::stoi(*decrypted_value));
            }
        } catch (const std::exception& e) {
            std::cerr << "Decryption failed for entry: " << e.what() << std::endl;
        }
    }
    
    return decrypted_results;
}