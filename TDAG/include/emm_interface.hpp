// 通用关键字加密引擎
#ifndef EMM_INTERFACE_HPP
#define EMM_INTERFACE_HPP

#include <vector>
#include <unordered_map>
#include <string>
#include "Setup_JXTp.hpp" // 复用VectorHash

// --- 通用类型别名 ---
using VectorHash = Setup_JXTp::VectorHash;
using Label = std::string;
using RecordID = int;
using KeywordMap = std::unordered_map<Label, std::vector<RecordID>>;
using Ciphertext = std::vector<unsigned char>;
using EncryptedIndex = std::unordered_map<Ciphertext, std::vector<Ciphertext>, VectorHash>;
using EncryptedResult = std::unordered_map<Ciphertext, std::vector<Ciphertext>, VectorHash>;

// EMM基础接口
/**
 * @class EMM_Interface
 * @brief Defines the abstract interface for a keyword-based Encrypted Multi-Map (EMM) engine.
 *
 * This interface specifies the fundamental operations required for any SSE scheme
 * that operates on a keyword-to-identifier mapping.
 */
class EMM_Interface {
public:
    virtual ~EMM_Interface() = default;
    
    // 构建明文倒排索引为密文
    virtual void buildEMM(const KeywordMap& plaintext_mm) = 0;
    
    // 生成查询token
    virtual std::vector<Ciphertext> generateTokens(const Label& keyword) const = 0;
    virtual std::vector<Ciphertext> generateTokens(const std::vector<Label>& keywords) const = 0;
    
    // 查询操作
    virtual EncryptedResult query(const std::vector<Ciphertext>& tokens) const = 0;
    
    // 获取加密索引
    virtual const EncryptedIndex& getEncryptedIndex() const = 0;
    
    // 获取存储开销统计
    /**
     * @brief 获取加密索引在服务器端的总存储大小。
     * @return 索引的总字节数。
     */
    virtual size_t getStorageSize() const = 0;

    virtual std::vector<RecordID> decryptResults(const EncryptedResult& encrypted_results) const = 0;
};


// 标准EMM实现
// class StandardEMM : public EMM_Interface {
// private:
//     std::unordered_map<std::vector<unsigned char>, 
//     std::vector<std::vector<unsigned char>>, VectorHash> tset;

//     std::string K_token,K_enc;
//                                             std::vector<unsigned char>, VectorHash>& encrypted_results) const = 0;
// };

// 标准EMM实现
// class StandardEMM : public EMM_Interface {
// private:
//     std::unordered_map<std::vector<unsigned char>, 
//     std::vector<std::vector<unsigned char>>, VectorHash> tset;

//     std::string K_token,K_enc;
    
// public:
//     StandardEMM(const std::string& k_token, const std::string& k_enc) 
//         : K_token(k_token), K_enc(k_enc) {}
    
//     // 从明文倒排索引到密文
//     void buildEMM(const std::unordered_map<std::string, std::vector<int>>& plaintext_mm) override;
    
//     // --- 单关键字版本 ---
//     std::vector<std::vector<unsigned char>> generateTokens(const std::string& keyword) const override;
    
//     // --- 新增：多关键字版本 ---
//     std::vector<std::vector<unsigned char>> generateTokens(const std::vector<std::string>& keywords) const;

//     std::unordered_map<std::vector<unsigned char>, std::vector<unsigned char>, VectorHash> 
//     query(const std::vector<std::vector<unsigned char>>& tokens) const override;
    
//     const std::unordered_map<std::vector<unsigned char>, 
//     std::vector<std::vector<unsigned char>>, VectorHash>& 
//     getEncryptedIndex() const override { return tset; }
    
//     size_t getStorageSize() const override;

//     std::vector<int> decryptResults(
//         const std::unordered_map<std::vector<unsigned char>, 
//         std::vector<unsigned char>, VectorHash>& encrypted_results) const override;
// };

#endif
