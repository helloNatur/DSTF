//分别有三部分token，一部分属于范围查询全部变为点查询，也就是时间范围变为点查询，
//一部分属于经纬度这个二维范围也变为点查询，但是这里已经包含在bf中
//最后一部分是keyword2的token

#include <gtest/gtest.h>
#include "Setup_JXTp.hpp"
#include "Server_JXTp.hpp"
#include "cube_code.h"
#include "bplus_tree.h"
#include "Hash.hpp"
#include "AESUtil.hpp"
#include "tool.hpp"
#include <vector>
#include <string>
#include <set>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>

class JXTpTest : public ::testing::Test {
protected:
    void SetUp() override {
        K_token = "89b7a92966f6eb32";
        K_w = "7975922666f6eb02";
        K_h = "9874a22554e7db85";
        K_aes = "8975924566f6e252";

        key_colnum = 7;
        join_column = 1;
        // record_num = 200000;
        record_num = 227428;
        condition = "";

        std::cout << "------------ JXT+ setup ------------\n";
        auto setup_start = std::chrono::high_resolution_clock::now();
        // 初始化 table_1 和 table_2
        table_1 = std::make_shared<Setup_JXTp>(1, key_colnum, join_column, record_num, condition);
        table_1->construct();
        auto& tset = table_1->getTset();
        table_2 = std::make_shared<Setup_JXTp>(2, key_colnum, join_column, record_num, condition);
        table_2->construct();
        tset.insert(table_2->getTset().begin(), table_2->getTset().end());

        join_attr1 = "userid";
        join_attr2 = "userid";
        start_time = "2012-04-03 18:00:00+00";
        end_time = "2012-04-08 10:10:10+00";
        lat_min = 40.7;
        lat_max = 40.76;
        lon_min = -74.0;
        lon_max = -73.98;
        // lat_min = 40.7;
        // lat_max = 40.71;
        // lon_min = -74.0;
        // lon_max = -73.99;
        keyword2 = "userid818"; 

        //TODO:server只接收t1
        server = std::make_shared<Server_JXTp>(tset, table_1->getF(), table_1->getCset(), table_2->getF(), table_2->getCset());
        bplus_tree = table_1->getBPlusTree();
        auto setup_end = std::chrono::high_resolution_clock::now();
        auto setup_duration = std::chrono::duration_cast<std::chrono::milliseconds>(setup_end - setup_start);
        std::cout << "Setup time: " << setup_duration.count() << " ms\n";
    }

    std::string_view K_token, K_w, K_h, K_aes;
    int key_colnum, join_column, record_num;
    std::string condition;
    std::shared_ptr<Setup_JXTp> table_1;
    std::shared_ptr<Setup_JXTp> table_2;
    std::string_view join_attr1, join_attr2;
    std::string start_time, end_time;
    std::string_view keyword2;
    double lat_min, lat_max, lon_min, lon_max;
    std::shared_ptr<Server_JXTp> server;
    std::shared_ptr<BPlusTree> bplus_tree;
};

// 测试 Setup_JXTp::construct 是否正确生成 明文倒排索引
// TEST_F(JXTpTest, ConstructTset) {
//     ASSERT_FALSE(table_1->getTset().empty()) << "table_1 tset should not be empty";
//     ASSERT_FALSE(table_2->getTset().empty()) << "table_2 tset should not be empty";

//     // 验证 tset 中是否包含空间编码
//     bool found_spatial = false;
//     for (const auto& [token, value] : table_1->getTset()) {
//         std::string token_str = Setup_JXTp::toBase64(token);
//         if (token_str.find("spatial_code") != std::string::npos) {
//             found_spatial = true;
//             std::cout << "Found spatial token: " << token_str << std::endl;
//             break;
//         }
//     }
//     EXPECT_TRUE(found_spatial) << "tset should contain spatial_code tokens";
// }

// 测试 CubeCode 编码一致性,以下通过测试，符合需求
// TEST_F(JXTpTest, CubeCodeConsistency) {
//     std::vector<double> min_vals = {0,0};
//     std::vector<double> max_vals = {10,10};
//     CubeCode cube_code(2, min_vals, max_vals, 3);

//     // 测试点
//     std::vector<double> point = {3,7};
//     auto data_codes = cube_code.generateDataCubeCodes(point);
//     ASSERT_EQ(data_codes.size(), 3) << "Expected 2 data code for level 3";
   
//     std::cout << std::endl;
//     std::vector<std::string> expected_codes = {
//         "2.500000,7.500000,1",
//         "3.750000,6.250000,2",
//         "3.125000,6.875000,3"
//     };

//     std::cout << "Data code: ";
//     for (const auto& code : data_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;

//     // 判断每个编码是否和预期一致
//     for (size_t i = 0; i < expected_codes.size(); ++i) {
//         EXPECT_EQ(data_codes[i], expected_codes[i]) 
//             << "Mismatch at level " << (i + 1);
//     }


//     // 测试查询范围
//     std::vector<double> query_min = {2,2};
//     std::vector<double> query_max = {7,7};
//     auto query_codes = cube_code.generateQueryCubeCodes(query_min, query_max);
//     ASSERT_FALSE(query_codes.empty()) << "Query codes should not be empty";
//     std::cout << "Query codes: ";
//     for (const auto& code : query_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;

//     //query_codes=["3.75,3.75,2",
//                 //  "3.75,6.25,2", "6.25,3.75,2", "6.25,6.25,2"
//                 // "3.125,5.625,3", "3.125,6.875,3", "4.375,5.625,3", "4.375,6.875,3",
//                 // "5.625,3.125,3","5.625,4.375,3","5.625,5.625,3","5.625,6.875,3",
//                 // "6.875,3.125,3","6.875,4.375,3","6.875,5.625,3","6.875,6.875,3",
//                 // "1.875,1.875,3", "1.875,3.125,3", "1.875,4.375,3", "1.875,5.625,3","1.875,6.875,3",
//                 // "3.125,1.875,3","4.375,1.875,3","5.625,1.875,3","6.875,1.875,3"]
    
//     // 验证数据点编码是否在查询编码中
//     bool any_matched = false;
//     for (const auto& dc : data_codes) {
//         if (std::find(query_codes.begin(), query_codes.end(), dc) != query_codes.end()) {
//             any_matched = true;
//             break;
//         }
//     }
//     EXPECT_TRUE(any_matched) << "None of the data codes matched query codes";
// }

// TEST_F(JXTpTest, CubeCodeLatLonConsistency) {
//     std::vector<double> min_vals = {40.5, -74.2};
//     std::vector<double> max_vals = {41.0, -73.7};
//     CubeCode cube_code(2, min_vals, max_vals, 3);

//     // 测试点
//     std::vector<double> point = {40.65,-74.1};
//     auto data_codes = cube_code.generateDataCubeCodes(point);
//     ASSERT_EQ(data_codes.size(), 3) << "Expected 2 data code for level 3";
   

//     std::cout << "Data code: ";
//     for (const auto& code : data_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;


//     // 测试查询范围
//     std::vector<double> query_min = {40.65, -74.11};
//     std::vector<double> query_max = {40.66, -74.09};
//     auto query_codes = cube_code.generateQueryCubeCodes(query_min, query_max);
//     ASSERT_FALSE(query_codes.empty()) << "Query codes should not be empty";
//     std::cout << "Query codes: ";
//     for (const auto& code : query_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;

//     bool any_matched = false;
//     for (const auto& dc : data_codes) {
//         if (std::find(query_codes.begin(), query_codes.end(), dc) != query_codes.end()) {
//             any_matched = true;
//             break;
//         }
//     }
//     EXPECT_TRUE(any_matched) << "None of the data codes matched query codes";
// }

// 测试 BPlusTree::query_sql
// TEST_F(JXTpTest, BPlusTreeQuerySql) {
//     auto id_tokens1 = table_1->getCandidateIntervals(start_time, end_time, lat_min, lat_max, lon_min, lon_max);
//     EXPECT_FALSE(id_tokens1.empty()) << "id_tokens1 should not be empty";

//     // for (const auto& interval : id_tokens1) {
//     //     int start_hour = interval.left / 6;
//     //     int start_min = (interval.left % 6) * 10;
//     //     int end_hour = interval.right / 6;
//     //     int end_min = (interval.right % 6) * 10;
//     //     std::cout << "Time Range: "
//     //               << std::setw(2) << std::setfill('0') << start_hour << ":"
//     //               << std::setw(2) << std::setfill('0') << start_min << " - "
//     //               << std::setw(2) << std::setfill('0') << end_hour << ":"
//     //               << std::setw(2) << std::setfill('0') << end_min
//     //               << ", Tokens: " << interval.tokens.size() << std::endl;
//     // }
// }

// 测试 JXTp::run 和 Server_JXTp::search
TEST_F(JXTpTest, JXTpRunAndSearch) {
    std::cout << "------------ JXT+ search ------------\n";
    double search_all = 0;
    int loop_count = 2; // 循环次数
    for(int x = 0; x < loop_count; ++x) {
        auto search_start = std::chrono::high_resolution_clock::now();
        auto join_hash1 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr1});
        auto join_hash2 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr2});

        // 计算时间范围对应的时间戳和区间
        // struct tm tm_start = {}, tm_end = {};
        // strptime(start_time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm_start);
        // strptime(end_time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm_end);
        // time_t start_t = timegm(&tm_start);
        // time_t end_t = timegm(&tm_end);
        // // long long start_ts = start_t - (start_t % 86400); // 精确到天
        // // long long end_ts = end_t - (end_t % 86400) + 86399;
        // int start_interval = tm_start.tm_hour * 6 + tm_start.tm_min / 10; // 10 分钟区间
        // int end_interval = tm_end.tm_hour * 6 + tm_end.tm_min / 10;

        // 生成空间编码，针对每一个转换为token，查看是否有符合的，但是最后需要取并集，再和时间戳的取交集
        // TODO:是否存在一种方案，不需要取交集
        std::vector<double> query_min = {lat_min, lon_min};
        std::vector<double> query_max = {lat_max, lon_max};
        auto query_codes = table_1->getBPlusTree()->getCubeCode()->
                                    generateQueryCubeCodes(query_min, query_max);
        std::vector<std::string> keyword1_list;
        for(const auto& code : query_codes) {
            // keyword1_list.push_back("spatial_code:" + code);
            keyword1_list.push_back(code);//都在第三层："40.718750,-73.981250,3"；"40.781250,-73.981250,3"
        }
        

        //有关于范围查询映射为点查询
        // Step 3.1: Compute id tokens for range query
        // 后续使用 server 的 segment_tree1 和 segment_tree2
        // auto id_tokens1 = table_1.getCandidateIntervals(id_start, id_end,std::string{keyword1});

        //TODO：这里还需要返回符合的时间戳区间且包含经纬度的关键词，为keyword0服务
        std::vector<SegmentTree::IntervalResult> id_tokens1;
        // for(const auto& keyword1:keyword1_list) {
        //     auto tokens = table_1->getCandidateIntervals(start_time, end_time, lat_min, lat_max, lon_min, lon_max);
        //     id_tokens1.insert(id_tokens1.end(), tokens.begin(), tokens.end());
        // }
        id_tokens1 = table_1->getCandidateIntervals(start_time, end_time, lat_min, lat_max, lon_min, lon_max); 
        if(x==0) std::cout << "id_tokens1.size() = " << id_tokens1.size() << std::endl;    //区间的size

        //为每个时间戳和区间生成join—token
        std::vector<std::vector<std::vector<unsigned char>>> join_token01(id_tokens1.size());
        std::vector<std::vector<std::vector<unsigned char>>> join_token02(id_tokens1.size());

        for (size_t i = 0; i < id_tokens1.size(); ++i) {
            const auto& interval = id_tokens1[i];
            int interval_idx = interval.left;
            //long long timestamp = start_ts + (interval_idx / 144) * 86400;
            //long long timestamp = Setup_JXTp::date_to_timestamp(start_time.substr(0, 10));
            long long timestamp = interval.day_ts;
            std::string keyword0 = "utctimestamp" + std::to_string(timestamp) + "_" + std::to_string(interval_idx);
            std::string stag_input0 = std::string{K_token} + keyword0 + std::string{join_attr1} + "1";
            auto stag0 = Hash::Get_SHA_256(stag_input0);

            int cnt0 = server->tset_table1_cnt(stag0);

            join_token01[i].resize(cnt0); //看里面tokens的数量
            join_token02[i].resize(cnt0);

            auto w0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword0} + "0");
            //TODO:去除table2的keyword，改为join-attr
            auto y0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword2} + "0");

            for (int j = 0; j < cnt0; ++j) {
                auto w_cnt0 = Hash::Get_SHA_256(std::string{K_w} + keyword0 + std::to_string(j + 1));
                join_token01[i][j] = tool::Xor(tool::Xor(w0, w_cnt0), join_hash1);
                join_token02[i][j] = tool::Xor(tool::Xor(y0, w_cnt0), join_hash2);
            }
        }

        // 解密并验证结果
        std::set<std::string> decrypted_range;
        std::set<std::string> decrypted_stag1;

        // 生成 stag1 和 join_token1, join_token2
        for(auto keyword1 : keyword1_list) { //73.996875,4 "40.734375,-73.996875,4" "40.703125,-73.965625,4" "40.734375,-73.965625,4" "40.765625,-73.996875,4" "40.765625,-73.965625,4"
            std::string stag_input1 = std::string{K_token} + keyword1 + std::string{join_attr1} + "1";
            auto stag1 = Hash::Get_SHA_256(stag_input1);
            int cnt1 = server->tset_table1_cnt(stag1);

            std::vector<std::vector<unsigned char>> join_token1(cnt1), join_token2(cnt1);
            auto w = Hash::Get_SHA_256(std::string{K_w} + keyword1 + "0");
            auto y = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword2} + "0");

            for (int i = 0; i < cnt1; ++i) {
                auto w_cnt = Hash::Get_SHA_256(std::string{K_w} + keyword1 + std::to_string(i + 1));
                join_token1[i] = tool::Xor(tool::Xor(w, w_cnt), join_hash1);
                join_token2[i] = tool::Xor(tool::Xor(y, w_cnt), join_hash2);
            }

            // 将 join_token01, join_token02, id_tokens1, join_token1, join_token2 和 stag1 传递给 server->search
            //TODO: 优化，在每次的循环中，只有 keyword1 变化，其他参数保持不变，相当于只有res_stag1 变化
            auto [res_range, res_stag1] = server->search(join_token01, join_token02, id_tokens1, join_token1, join_token2, stag1);
            auto k_dec1 = Hash::Get_SHA_256(std::string{K_aes} + std::string{keyword1});
            auto k_dec2 = Hash::Get_SHA_256(std::string{K_aes} + std::string{keyword2});

            // 验证结果非空
            // EXPECT_FALSE(res_range.empty()) << "res_range should not be empty";
            // EXPECT_FALSE(res_stag1.empty()) << "res_stag1 should not be empty";

            // 打印结果
            // if (x == 0) std::cout << "res_range size: " << res_range.size() << ", res_stag1 size: " << res_stag1.size() << std::endl;

            //计算查询范围的密钥
            std::vector<std::vector<std::vector<unsigned char>>> k_dec01(id_tokens1.size());
            std::vector<std::vector<std::vector<unsigned char>>> k_dec02(id_tokens1.size());
            for (size_t i = 0; i < id_tokens1.size(); ++i) {
                const auto& interval = id_tokens1[i];
                int interval_idx = interval.left;
                // long long timestamp = start_ts + (interval_idx / 144) * 86400;
                long long timestamp = interval.day_ts;
                std::string keyword0 = "utctimestamp" + std::to_string(timestamp) + "_" + std::to_string(interval_idx);
                std::string k_dec_input01 = std::string{K_aes} + keyword0;
                std::string k_dec_input02 = std::string{K_aes} + std::string{keyword2};
                k_dec01[i].resize(res_range.size());
                k_dec02[i].resize(res_range.size());
                for (size_t j = 0; j < res_range.size(); ++j) {
                    k_dec01[i][j] = Hash::Get_SHA_256(k_dec_input01);
                    k_dec02[i][j] = Hash::Get_SHA_256(k_dec_input02);
                }
            }
            
            // if (x == 0) std::cout << "k_dec01 size: " << k_dec01.size() << ", k_dec02 size: " << k_dec02.size() << std::endl;

            for (size_t i = 0; i < res_range.size(); ++i) {
                int key_t = i/2;
                
                if (key_t >= id_tokens1.size() || key_t >= k_dec01.size() || key_t >= k_dec02.size()) {
                    // std::cerr << "Error: key_t " << key_t << " out of bounds at i=" << i << std::endl;
                    continue;
                }
                const auto& res_i = res_range[i];
                if (i % 2 == 0) {
                    for (const auto& r : res_i) {
                        if (auto decrypted = AESUtil::decrypt(k_dec01[key_t][0], r);  decrypted) {
                            decrypted_range.insert(*decrypted);
                            // std::cout << *decrypted << ",\n";
                            // outfile << *decrypted << "," << '\n';  // table1 有值，table2 空
                        }
                    }
                } else {
                    for (const auto& r : res_i) {
                        if (auto decrypted = AESUtil::decrypt(k_dec02[key_t][0], r);  decrypted) {
                            decrypted_range.insert(*decrypted);
                            // std::cout << *decrypted << ",\n";
                            // outfile << "," << *decrypted << '\n';  // table1 空，table2 有值
                        }
                    }
                }
            }

            for (size_t i = 0; i < res_stag1.size(); ++i) {
                const auto& res_i = res_stag1[i];
                if (i % 2 == 0) {
                    for (const auto& r : res_i) {
                        if (auto decrypted = AESUtil::decrypt(k_dec1, r);  decrypted) {
                            decrypted_stag1.insert(*decrypted);
                            // std::cout << *decrypted << ",\n";
                        }
                    }
                } else {
                    for (const auto& r : res_i) {
                        if (auto decrypted = AESUtil::decrypt(k_dec2, r);  decrypted) {
                            decrypted_stag1.insert(*decrypted);
                            // std::cout << *decrypted << ",\n";
                        }
                    }
                }
            }
        }

        // // outfile.close();
        // std::set<std::string> final_result;
        // std::set_intersection(
        //     decrypted_range.begin(), decrypted_range.end(),
        //     decrypted_stag1.begin(), decrypted_stag1.end(),
        //     std::inserter(final_result, final_result.begin())
        // );
            
        // // 读取预期结果:以下部分属于验证
        // std::set<std::string> expected_range;
        // // std::ifstream file("/nvme/baum/git-project/JXT2/data/Spatio-Temporal-Datasets/filtered_ids_200000.csv"); // 替换为实际路径
        // std::ifstream file("/nvme/baum/git-project/JXT2/data/Spatio-Temporal-Datasets/filtered_ids_all.csv");
        // if (!file.is_open()) {
        //     FAIL() << "Failed to open CSV file for expected results";
        // }
        // std::string line;
        // std::getline(file, line); // 跳过表头
        // while (std::getline(file, line)) {
        //     std::istringstream iss(line);
        //     std::string id1, id2;
        //     if (std::getline(iss, id1, ',') && std::getline(iss, id2, ',')) {
        //         expected_range.insert(id1);
        //         expected_range.insert(id2);
        //     }
        // }
        // file.close();

        // EXPECT_EQ(final_result, expected_range) << "Decrypted results should match expected results";
        auto search_end = std::chrono::high_resolution_clock::now();
        search_all+= std::chrono::duration<double, std::nano>(search_end - search_start).count();
        // if (x == 0) std::cout << "res_range size: " << res_range.size() << ", res_stag1 size: " << res_stag1.size() << "\n";
        // if (x == 0) std::cout << "final result size: " << final_result.size() << "\n";
    }
    std::cout << "JXT+ average search time: " << search_all / (loop_count* 1e6) << " ms\n";

    // 读取预期结果:以下部分属于验证
    // std::set<std::string> expected_range;
    // std::ifstream file("/nvme/baum/git-project/JXT2/data/Spatio-Temporal-Datasets/filtered_ids_all.csv"); // 替换为实际路径
    // if (!file.is_open()) {
    //     FAIL() << "Failed to open CSV file for expected results";
    // }
    // std::string line;
    // std::getline(file, line); // 跳过表头
    // while (std::getline(file, line)) {
    //     std::istringstream iss(line);
    //     std::string id1, id2;
    //     if (std::getline(iss, id1, ',') && std::getline(iss, id2, ',')) {
    //         expected_range.insert(id1);
    //         expected_range.insert(id2);
    //     }
    // }
    // file.close();

    // EXPECT_EQ(final_result, expected_range) << "Decrypted results should match expected results";
}

// 测试 stag1 是否在 tset 中
// TEST_F(JXTpTest, Stag1InTset) {
//     std::vector<double> query_min = {lat_min, lon_min};
//     std::vector<double> query_max = {lat_max, lon_max};
//     auto query_codes = table_1->getBPlusTree()->getCubeCode()->generateQueryCubeCodes(query_min, query_max);
//     ASSERT_FALSE(query_codes.empty()) << "Query codes should not be empty";

//     // std::string keyword1 = "spatial_code:" + query_codes[0];
//     std::string keyword1 =  query_codes[0]; // 直接使用编码，不加前缀
//     std::string stag_input1 = std::string{K_token} + keyword1 + std::string{join_attr1} + "1";
//     auto stag1 = Hash::Get_SHA_256(stag_input1);
//     std::cout << "Generated stag1: " << Setup_JXTp::toBase64(stag1) << std::endl;

//     auto& tset = table_1->getTset();
//     auto it = tset.find(stag1);
//     EXPECT_NE(it, tset.end()) << "stag1 should be found in tset for keyword: " << keyword1;
// }

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}