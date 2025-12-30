#include <gtest/gtest.h>
#include "standard_emm.hpp"
#include "Hash.hpp"
#include "AESUtil.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <set>

class EMMTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 使用固定的密钥进行测试
        K_token = "89b7a92966f6eb32";
        K_enc = "7975922666f6eb02";
        
        emm = std::make_shared<StandardEMM>(K_token, K_enc);
        
        // 生成测试数据
        generateTestData();
    }
    
    void generateTestData() {
        // 创建明文倒排索引
        plaintext_mm = {
            {"keyword1", {1, 2, 3, 4, 5}},
            {"keyword2", {6, 7, 8}},
            {"keyword3", {9, 10}},
            {"keyword4", {11, 12, 13, 14, 15, 16, 17}},
            {"empty_keyword", {}},  // 空关键词测试
            {"single_record", {100}}  // 单记录测试
        };
        
        // 生成大规模测试数据
        std::random_device rd;
        std::mt19937 gen(42); // 固定种子
        std::uniform_int_distribution<> dis(1, 10000);
        
        for (int i = 0; i < 100; ++i) {
            std::string keyword = "large_keyword_" + std::to_string(i);
            std::vector<int> records;
            int record_count = dis(gen) % 50 + 1; // 1-50个记录
            
            for (int j = 0; j < record_count; ++j) {
                records.push_back(dis(gen));
            }
            
            large_plaintext_mm[keyword] = records;
        }
    }
    
    std::string K_token, K_enc;
    std::shared_ptr<StandardEMM> emm;
    std::unordered_map<std::string, std::vector<int>> plaintext_mm;
    std::unordered_map<std::string, std::vector<int>> large_plaintext_mm;
};

// 基础构建测试
TEST_F(EMMTest, BasicBuildTest) {
    std::cout << "\n=== Basic Build Test ===" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    emm->buildEMM(plaintext_mm);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Build time: " << duration.count() << " ms" << std::endl;
    
    // 验证存储大小
    size_t storage_size = emm->getStorageSize();
    std::cout << "Storage size: " << storage_size << " bytes" << std::endl;
    EXPECT_GT(storage_size, 0) << "Storage size should be greater than 0";
    
    // 验证加密索引不为空
    const auto& encrypted_index = emm->getEncryptedIndex();
    EXPECT_FALSE(encrypted_index.empty()) << "Encrypted index should not be empty";
    
    std::cout << "Encrypted index entries: " << encrypted_index.size() << std::endl;
}

// 查询功能测试
TEST_F(EMMTest, QueryTest) {
    std::cout << "\n=== Query Test ===" << std::endl;
    
    // 构建EMM
    emm->buildEMM(plaintext_mm);
    
    // 测试存在的关键词
    std::vector<std::string> test_keywords = {"keyword1", "keyword2", "keyword3", "keyword4"};
    
    for (const auto& keyword : test_keywords) {
        std::cout << "\nTesting keyword: " << keyword << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 生成查询token
        auto tokens = emm->generateTokens(keyword);
        EXPECT_FALSE(tokens.empty()) << "Tokens should not be empty for keyword: " << keyword;
        
        // 执行查询
        auto encrypted_results = emm->query(tokens);
        
        // 解密结果
        auto decrypted_results = emm->decryptResults(encrypted_results);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        std::cout << "Query time: " << duration.count() << " μs" << std::endl;
        std::cout << "Encrypted results: " << encrypted_results.size() << std::endl;
        std::cout << "Decrypted results: " << decrypted_results.size() << std::endl;
        
        // 验证结果正确性
        const auto& expected_results = plaintext_mm.at(keyword);
        EXPECT_EQ(decrypted_results.size(), expected_results.size()) 
            << "Result count mismatch for keyword: " << keyword;
        
        // 验证结果内容（顺序可能不同）
        std::set<int> expected_set(expected_results.begin(), expected_results.end());
        std::set<int> actual_set(decrypted_results.begin(), decrypted_results.end());
        EXPECT_EQ(expected_set, actual_set) << "Result content mismatch for keyword: " << keyword;
        
        // 打印前几个结果
        std::cout << "Results: ";
        for (size_t i = 0; i < std::min(decrypted_results.size(), size_t(10)); ++i) {
            std::cout << decrypted_results[i] << " ";
        }
        if (decrypted_results.size() > 10) std::cout << "...";
        std::cout << std::endl;
    }
}

// 不存在关键词测试
TEST_F(EMMTest, NonExistentKeywordTest) {
    std::cout << "\n=== Non-Existent Keyword Test ===" << std::endl;
    
    emm->buildEMM(plaintext_mm);
    
    std::vector<std::string> non_existent_keywords = {
        "non_existent_keyword",
        "another_missing_keyword",
        "",  // 空字符串
        "keyword_with_special_chars!@#$%"
    };
    
    for (const auto& keyword : non_existent_keywords) {
        std::cout << "Testing non-existent keyword: '" << keyword << "'" << std::endl;
        
        auto tokens = emm->generateTokens(keyword);
        auto encrypted_results = emm->query(tokens);
        auto decrypted_results = emm->decryptResults(encrypted_results);
        
        EXPECT_TRUE(decrypted_results.empty()) 
            << "Non-existent keyword should return empty results: " << keyword;
        
        std::cout << "Results (should be empty): " << decrypted_results.size() << std::endl;
    }
}

// 空关键词和单记录测试
TEST_F(EMMTest, EdgeCasesTest) {
    std::cout << "\n=== Edge Cases Test ===" << std::endl;
    
    emm->buildEMM(plaintext_mm);
    
    // 测试单记录关键词
    {
        std::cout << "Testing single record keyword..." << std::endl;
        auto tokens = emm->generateTokens("single_record");
        auto encrypted_results = emm->query(tokens);
        auto decrypted_results = emm->decryptResults(encrypted_results);
        
        EXPECT_EQ(decrypted_results.size(), 1) << "Single record keyword should return 1 result";
        EXPECT_EQ(decrypted_results[0], 100) << "Single record should match expected value";
        
        std::cout << "Single record result: " << decrypted_results[0] << std::endl;
    }
    
    // 测试空EMM
    {
        std::cout << "Testing empty EMM..." << std::endl;
        std::unordered_map<std::string, std::vector<int>> empty_mm;
        auto empty_emm = std::make_unique<StandardEMM>(K_token, K_enc);
        empty_emm->buildEMM(empty_mm);
        
        auto tokens = empty_emm->generateTokens("any_keyword");
        auto encrypted_results = empty_emm->query(tokens);
        auto decrypted_results = empty_emm->decryptResults(encrypted_results);
        
        EXPECT_TRUE(decrypted_results.empty()) << "Empty EMM should return no results";
        EXPECT_EQ(empty_emm->getStorageSize(), 0) << "Empty EMM should have zero storage";
    }
}

// 大规模数据测试
TEST_F(EMMTest, LargeScaleTest) {
    std::cout << "\n=== Large Scale Test ===" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    emm->buildEMM(large_plaintext_mm);
    auto build_end_time = std::chrono::high_resolution_clock::now();
    
    auto build_duration = std::chrono::duration_cast<std::chrono::milliseconds>(build_end_time - start_time);
    std::cout << "Large scale build time: " << build_duration.count() << " ms" << std::endl;
    
    size_t storage_size = emm->getStorageSize();
    std::cout << "Large scale storage size: " << storage_size << " bytes" << std::endl;
    
    // 计算总记录数
    size_t total_records = 0;
    for (const auto& [keyword, records] : large_plaintext_mm) {
        total_records += records.size();
    }
    std::cout << "Total records: " << total_records << std::endl;
    std::cout << "Average bytes per record: " << static_cast<double>(storage_size) / total_records << std::endl;
    
    // 随机测试一些关键词
    std::vector<std::string> test_keywords;
    int test_count = 0;
    for (const auto& [keyword, records] : large_plaintext_mm) {
        if (test_count++ >= 10) break; // 只测试前10个
        test_keywords.push_back(keyword);
    }
    
    auto query_start_time = std::chrono::high_resolution_clock::now();
    size_t total_results = 0;
    
    for (const auto& keyword : test_keywords) {
        auto tokens = emm->generateTokens(keyword);
        auto encrypted_results = emm->query(tokens);
        auto decrypted_results = emm->decryptResults(encrypted_results);
        
        total_results += decrypted_results.size();
        
        // 验证结果正确性
        const auto& expected_results = large_plaintext_mm.at(keyword);
        EXPECT_EQ(decrypted_results.size(), expected_results.size()) 
            << "Large scale result count mismatch for keyword: " << keyword;
    }
    
    auto query_end_time = std::chrono::high_resolution_clock::now();
    auto query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(query_end_time - query_start_time);
    
    std::cout << "Query time for " << test_keywords.size() << " keywords: " << query_duration.count() << " ms" << std::endl;
    std::cout << "Average query time: " << static_cast<double>(query_duration.count()) / test_keywords.size() << " ms" << std::endl;
    std::cout << "Total results retrieved: " << total_results << std::endl;
}

// // 性能基准测试
// TEST_F(EMMTest, PerformanceBenchmark) {
//     std::cout << "\n=== Performance Benchmark ===" << std::endl;
    
//     // 创建不同规模的测试数据
//     std::vector<std::pair<int, int>> test_scales = {
//         {10, 10},      // 10个关键词，每个10条记录
//         {100, 20},     // 100个关键词，每个20条记录
//         {500, 50},     // 500个关键词，每个50条记录
//     };
    
//     for (const auto& [keyword_count, records_per_keyword] : test_scales) {
//         std::cout << "\nTesting scale: " << keyword_count << " keywords, " 
//                   << records_per_keyword << " records each" << std::endl;
        
//         // 生成测试数据
//         std::unordered_map<std::string, std::vector<int>> test_data;
//         std::random_device rd;
//         std::mt19937 gen(42);
//         std::uniform_int_distribution<> dis(1, 100000);
        
//         for (int i = 0; i < keyword_count; ++i) {
//             std::string keyword = "perf_keyword_" + std::to_string(i);
//             std::vector<int> records;
//             for (int j = 0; j < records_per_keyword; ++j) {
//                 records.push_back(dis(gen));
//             }
//             test_data[keyword] = records;
//         }
        
//         // 构建性能测试
//         auto perf_emm = std::make_unique<StandardEMM>(K_token, K_enc);
        
//         auto build_start = std::chrono::high_resolution_clock::now();
//         perf_emm->buildEMM(test_data);
//         auto build_end = std::chrono::high_resolution_clock::now();
        
//         auto build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start);
//         std::cout << "Build time: " << build_time.count() << " ms" << std::endl;
        
//         // 查询性能测试
//         std::vector<std::string> query_keywords;
//         for (int i = 0; i < std::min(keyword_count, 20); ++i) {
//             query_keywords.push_back("perf_keyword_" + std::to_string(i));
//         }
        
//         auto query_start = std::chrono::high_resolution_clock::now();
//         for (const auto& keyword : query_keywords) {
//             auto tokens = perf_emm->generateTokens(keyword);
//             auto encrypted_results = perf_emm->query(tokens);
//             auto decrypted_results = perf_emm->decryptResults(encrypted_results);
//         }
//         auto query_end = std::chrono::high_resolution_clock::now();
        
//         auto query_time = std::chrono::duration_cast<std::chrono::microseconds>(query_end - query_start);
//         std::cout << "Query time for " << query_keywords.size() << " keywords: " 
//                   << query_time.count() << " μs" << std::endl;
//         std::cout << "Average query time: " 
//                   << static_cast<double>(query_time.count()) / query_keywords.size() << " μs" << std::endl;
        
//         std::cout << "Storage size: " << perf_emm->getStorageSize() << " bytes" << std::endl;
//     }
// }

// 加密正确性测试
TEST_F(EMMTest, EncryptionCorrectnessTest) {
    std::cout << "\n=== Encryption Correctness Test ===" << std::endl;
    
    emm->buildEMM(plaintext_mm);
    
    // 多次查询同一关键词，结果应该一致
    std::string test_keyword = "keyword1";
    std::vector<std::vector<int>> multiple_results;
    
    for (int i = 0; i < 5; ++i) {
        auto tokens = emm->generateTokens(test_keyword);
        auto encrypted_results = emm->query(tokens);
        auto decrypted_results = emm->decryptResults(encrypted_results);
        multiple_results.push_back(decrypted_results);
    }
    
    // 验证所有结果都相同
    for (size_t i = 1; i < multiple_results.size(); ++i) {
        EXPECT_EQ(multiple_results[0].size(), multiple_results[i].size()) 
            << "Multiple queries should return same number of results";
        
        std::set<int> set0(multiple_results[0].begin(), multiple_results[0].end());
        std::set<int> seti(multiple_results[i].begin(), multiple_results[i].end());
        EXPECT_EQ(set0, seti) << "Multiple queries should return same results";
    }
    
    std::cout << "Consistency test passed for " << multiple_results.size() << " queries" << std::endl;
    
    // 测试不同密钥产生不同结果
    auto different_emm = std::make_unique<StandardEMM>("different_token", "different_enc");
    different_emm->buildEMM(plaintext_mm);
    
    auto tokens1 = emm->generateTokens(test_keyword);
    auto tokens2 = different_emm->generateTokens(test_keyword);
    
    EXPECT_NE(tokens1, tokens2) << "Different keys should produce different tokens";
    
    std::cout << "Different key test passed" << std::endl;
}

// 新增：多关键字查询测试
TEST_F(EMMTest, MultiKeywordQueryTest) {
    std::cout << "\n=== Multi-Keyword Query Test ===" << std::endl;
    
    // 构建 EMM
    emm->buildEMM(plaintext_mm);
    
    // 定义要查询的多个关键字
    std::vector<std::string> keywords_to_query = {"keyword1", "keyword3"};
    std::cout << "Querying for keywords: 'keyword1' and 'keyword3'" << std::endl;
    
    // 使用新的重载函数生成 tokens
    auto tokens = emm->generateTokens(keywords_to_query);
    EXPECT_EQ(tokens.size(), 2) << "Should generate two tokens for two keywords";
    
    // 执行查询
    auto encrypted_results = emm->query(tokens);
    
    // 解密结果
    auto decrypted_results = emm->decryptResults(encrypted_results);
    
    // 验证结果
    // 预期结果应该是 keyword1 和 keyword3 结果的并集
    auto& res1 = plaintext_mm.at("keyword1"); // {1, 2, 3, 4, 5}
    auto& res3 = plaintext_mm.at("keyword3"); // {9, 10}
    
    std::set<int> expected_set;
    expected_set.insert(res1.begin(), res1.end());
    expected_set.insert(res3.begin(), res3.end());
    
    std::set<int> actual_set(decrypted_results.begin(), decrypted_results.end());
    
    EXPECT_EQ(actual_set.size(), expected_set.size()) << "Result count should be the sum of unique records";
    EXPECT_EQ(actual_set, expected_set) << "The decrypted results should match the union of expected results";
    
    std::cout << "Expected results count: " << expected_set.size() << std::endl;
    std::cout << "Actual results count: " << actual_set.size() << std::endl;
    std::cout << "Multi-keyword query test passed." << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "Starting EMM Interface Tests..." << std::endl;
    std::cout << "=================================" << std::endl;
    
    return RUN_ALL_TESTS();
}
