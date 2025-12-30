#include <gtest/gtest.h>
#include "SegmentTree.h"
#include "Setup_JXTp.hpp"
#include <iostream>
#include <vector>
#include <map>
#include <string_view>
#include <cmath>
#include "Server_JXTp.hpp"
#include "Hash.hpp"
#include "AESUtil.hpp"
#include "tool.hpp"
#include <set>
#include <fstream>  // for std::ofstream


class IdTokenTest : public ::testing::Test {
protected:
    void SetUp() override {
        K_token = "89b7a92966f6eb32";
        K_w = "7975922666f6eb02";
        K_h = "9874a22554e7db85";
        K_aes = "8975924566f6e252";

        constexpr int key_colnum = 9;
        constexpr int join_column = 1;
        constexpr int record_num = 1 << 16; // 65536
        std::string condition;

        Setup_JXTp table_1{1, key_colnum, join_column, record_num, condition};
        table_1.construct();
        //table_1.saveToJson("output1");
        auto& tset = table_1.getTset(); // 使用引用避免拷贝

        Setup_JXTp table_2{2, key_colnum, join_column, record_num, condition};
        table_2.construct();
        //table_1.saveToJson("output2");
        tset.insert(table_2.getTset().begin(), table_2.getTset().end()); // 替代 merge

        id_start = 0;
        id_end = 2000;

        server = std::make_shared<Server_JXTp>(tset, table_1.getF(), table_1.getCset(), table_2.getF(), table_2.getCset());
        
        segment_tree = table_1.getSegmentTree();

        id_tokens1 = table_1.queryTree(id_start, id_end);
    }

    int id_start;
    int id_end;
    std::string_view K_token,K_w,K_h,K_aes;
    std::shared_ptr<SegmentTree> segment_tree;
    std::vector<std::vector<unsigned char>> id_tokens1;
    std::shared_ptr<Server_JXTp> server; 
};

// Test constructor and tree initialization
TEST_F(IdTokenTest, ConstructorAndInitialization) {
    ASSERT_NE(segment_tree,nullptr)<< "SegmentTree should be initialized";
    EXPECT_EQ(segment_tree->get_size(), 65536)<< "SegmentTree size should be 65536"; // Assuming size is 65536
    EXPECT_GE(segment_tree->get_tree().size(), 4 * 65536)<< "SegmentTree size should be at least 4 * n"; // Tree size should be at least 4 * n
}

TEST_F(IdTokenTest, IdTokensSizeAndContent) {
    ASSERT_FALSE(id_tokens1.empty()) << "id_tokens1 should not be empty";
    EXPECT_GE(id_tokens1.size(), 2001) << "id_tokens1 size should be at least 2001";
    
    int i=0;

    for(const auto& token : id_tokens1) {
        //初始化阶段在0-2000之间的所有id_tokens1
        std::string keyword0 = "id" + std::string("table1_id_") + std::to_string(i + id_start);
        auto set_id_token = Hash::Get_SHA_256(std::string{K_token} + keyword0 + "join-attr0" + "1");
        EXPECT_EQ(token, set_id_token) << "id_tokens1 should match the expected token for id " << i;
        ++i;
    }
}

TEST_F(IdTokenTest, SearchWithIdTokens) {
    std::string_view keyword2 = "keyword0table2_keyword_0_0";
    std::string_view join_attr1 = "join-attr0";
    std::string_view join_attr2 = "join-attr0";

    auto join_hash1 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr1});
    auto join_hash2 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr2});

    //有关于范围查询映射为点查询
    std::vector<std::vector<std::vector<unsigned char>>> join_token01(id_end - id_start + 1);
    std::vector<std::vector<std::vector<unsigned char>>> join_token02(id_end - id_start + 1);

    for (int i = 0; i < id_end-id_start+1; ++i) {
        std::string keyword0 = "id" + std::string("table1_id_") + std::to_string(i + id_start);
        std::string stag_input0 = std::string{K_token} + keyword0 + std::string{join_attr1} + "1";
        auto stag0 = Hash::Get_SHA_256(stag_input0); 

        int cnt0 = server->tset_table1_cnt(stag0);  //cnt0==1

        EXPECT_EQ(cnt0, 1) << "Expected cnt0 to be 1 for keyword0: " << keyword0;

        join_token01[i].resize(cnt0);  // 初始化内层向量
        join_token02[i].resize(cnt0);  

        auto w0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword0} + "0");
        auto y0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword2} + "0");

        //计算jointokens
        for (int j = 0; j < cnt0; ++j) {
            auto w_cnt0 = Hash::Get_SHA_256(std::string{K_w} + keyword0 + std::to_string(j + 1));
            join_token01[i][j] = tool::Xor(tool::Xor(w0, w_cnt0), join_hash1);
            join_token02[i][j] = tool::Xor(tool::Xor(y0, w_cnt0), join_hash2);
        }
        
    }

    auto [res_range, res_stag1] = server->search(join_token01, join_token02, id_tokens1, {}, {});

    EXPECT_FALSE(res_range.empty()) << "Search result should not be empty";
    EXPECT_EQ(res_range.size(), 2000) << "Expected 2000 results in res_range";

    std::vector<std::vector<std::vector<unsigned char>>> k_dec01(id_end - id_start+1);
    std::vector<std::vector<std::vector<unsigned char>>> k_dec02(id_end - id_start+1);
    for (int i = 0; i < id_end-id_start+1; ++i) {
        std::string keyword0 = "id" + std::string("table1_id_") + std::to_string(i + id_start);
        // 分配内层向量空间（假设每个 i 只需要一个解密密钥）
        k_dec01[i].resize(1);
        k_dec02[i].resize(1);

        k_dec01[i][0] = Hash::Get_SHA_256(std::string{K_aes} + std::string{keyword0});
        k_dec02[i][0] = Hash::Get_SHA_256(std::string{K_aes} + std::string{keyword2}); 
    }

    std::set<std::string> decrypted_range;
    std::set<std::string> expected_range;
    // 解密并验证
    for (size_t i = 0; i < res_range.size(); ++i) {
        int key_t = i/2;
        const auto& res_i = res_range[i];
        if (i % 2 == 0) {
            for (const auto& r : res_i) {
                if (auto decrypted = AESUtil::decrypt(k_dec01[key_t][0], r);decrypted) {
                    decrypted_range.insert(*decrypted);
                }
            }
        } else {
            for (const auto& r : res_i) {
                if (auto decrypted = AESUtil::decrypt(k_dec02[key_t][0], r); decrypted) {
                    decrypted_range.insert(*decrypted);
                }
            }
        }
    }

    std::ifstream file("/home/baum/encdb/JXT/data/pg_0-2000.csv");
    if (!file.is_open()) {
        FAIL() << "Failed to open the CSV file for expected tokens.";
    }
    std::string line;

    // 读取表头
    std::getline(file, line);

    // 逐行处理数据
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string id1, id2;

        // 解析逗号分隔的两个字段
        if (std::getline(iss, id1, ',') && std::getline(iss, id2, ',')) {
            expected_range.insert(id1);
            expected_range.insert(id2);
        }
    }
        
    EXPECT_EQ(decrypted_range, expected_range) << "Decrypted token should match expected token for id " ;

}

