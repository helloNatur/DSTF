#include <iostream>
#include <chrono>
#include <vector>
#include <map>
#include <string_view>
#include <cmath>
#include "Setup_JXTp.hpp"
#include "Server_JXTp.hpp"
#include "Hash.hpp"
#include "AESUtil.hpp"
#include "tool.hpp"
#include <set>
#include <fstream>  // for std::ofstream

class JXTp {
private:
    inline static const std::string_view K_token = "89b7a92966f6eb32";
    inline static const std::string_view K_w = "7975922666f6eb02";
    inline static const std::string_view K_h = "9874a22554e7db85";
    inline static const std::string_view K_aes = "8975924566f6e252";

public:
    static void run() {
        // Step 1: Prepare parameters
        constexpr int key_colnum = 9;
        constexpr int join_column = 1;
        //constexpr int record_num = 1 << 16; // 65536
        constexpr int record_num = 15000; // 65536
        std::string condition;

        std::cout << "------------- JXT+ ---------------\n"
                  << "------------ JXT+ setup ------------\n";

        // Step 2: Setup phase
        auto setup_start = std::chrono::high_resolution_clock::now();
        Setup_JXTp table_1{1, key_colnum, join_column, record_num, condition};
        table_1.construct();
        //table_1.saveToJson("output1");
        auto& tset = table_1.getTset(); // 使用引用避免拷贝

        Setup_JXTp table_2{2, key_colnum, join_column, record_num, condition};
        table_2.construct();
        //table_1.saveToJson("output2");
        tset.insert(table_2.getTset().begin(), table_2.getTset().end()); // 替代 merge


        auto setup_end = std::chrono::high_resolution_clock::now();
        std::cout << "JXT+ setup time: " << std::chrono::duration<double, std::milli>(setup_end - setup_start).count() << " ms\n";

        // Step 3: Search phase
        std::cout << "------------ JXT+ search ------------\n";
        std::string_view keyword1 = "keyword0table1_keyword_0_0";//1000变一次
        std::string_view keyword2 = "keyword0table2_keyword_0_0";
        std::string_view join_attr1 = "join-attr0";
        std::string_view join_attr2 = "join-attr0";
        int id_start = 0;
        int id_end = 2000;

        double search_all = 0;
        for (int x = 0; x < 1000; ++x) {
            auto search_start = std::chrono::high_resolution_clock::now();
            // Server_JXTp server{tset, table_1.getF(), table_1.getCset(), table_2.getF(), table_2.getCset(),
            //     std::move(table_1.getSegmentTree()), std::move(table_2.getSegmentTree())};

            Server_JXTp server{tset, table_1.getF(), table_1.getCset(), table_2.getF(), table_2.getCset()};

            auto join_hash1 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr1});
            auto join_hash2 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr2});

            //有关于范围查询映射为点查询
            // Step 3.1: Compute id tokens for range query
            // 后续使用 server 的 segment_tree1 和 segment_tree2
            auto id_tokens1 = table_1.getCandidateIntervals(id_start, id_end,std::string{keyword1});
            //std::cout << "id_tokens1 size: " << id_tokens1.size() << std::endl;
            // for (const auto& token : id_tokens1) {
            //     std::cout << "Token size: " << token.size() << std::endl;
            // }
            // auto id_tokens2 = server.queryTree2(id_start, id_end);
            //TODO：实际值
            std::vector<std::vector<std::vector<unsigned char>>> join_token01(1000);
            std::vector<std::vector<std::vector<unsigned char>>> join_token02(1000);

            //std::vector<int> cnt0_list;
            //TODO：1000需要改为变量join_token01.size()
            for (int i = 0; i < 1000; ++i) {
                std::string keyword0 = "id" + std::string("table1_id_") + std::to_string(i + id_start);
                std::string stag_input0 = std::string{K_token} + keyword0 + std::string{join_attr1} + "1";
                auto stag0 = Hash::Get_SHA_256(stag_input0); 

                int cnt0 = server.tset_table1_cnt(stag0);  //cnt0==1
                //cnt0_list.push_back(cnt0);

                if (cnt0 != 1) {
                    std::cerr << "Error: cnt0 is not 1 for keyword0: " << keyword0 << ", at i = " << i << std::endl;
                    std::exit(1);  // 立即终止程序，返回状态码 1
                }

                join_token01[i].resize(cnt0);  // 初始化内层向量
                join_token02[i].resize(cnt0);  

                std::string_view keyword2 = "keyword0table2_keyword_0_0";
                auto w0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword0} + "0");
                auto y0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword2} + "0");

                //计算jointokens
                for (int j = 0; j < cnt0; ++j) {
                    auto w_cnt0 = Hash::Get_SHA_256(std::string{K_w} + keyword0 + std::to_string(j + 1));
                    join_token01[i][j] = tool::Xor(tool::Xor(w0, w_cnt0), join_hash1);
                    join_token02[i][j] = tool::Xor(tool::Xor(y0, w_cnt0), join_hash2);
                }
                
            }
            // std::cout << "join_token01 size: " << join_token01.size() << std::endl; 
            // std::cout << "join_token02 size: " << join_token02.size() << std::endl;

            // Step 3.2: Compute stag
            std::string stag_input1 = std::string{K_token} + std::string{keyword1} + std::string{join_attr1} + "1";
            auto stag1 = Hash::Get_SHA_256(stag_input1);
            int cnt1 = server.tset_table1_cnt(stag1);


            std::vector<std::vector<unsigned char>> join_token1(cnt1), join_token2(cnt1);

            auto w = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword1} + "0");
            auto y = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword2} + "0");
            

            // Step 3.3: Compute joinTokens
            for (int i = 0; i < cnt1; ++i) {
                auto w_cnt = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword1} + std::to_string(i + 1));
                join_token1[i] = tool::Xor(tool::Xor(w, w_cnt), join_hash1);
                join_token2[i] = tool::Xor(tool::Xor(y, w_cnt), join_hash2);
            }

            // Step 3.4: Server search with id range filtering
            //Todo: 保存join_token0，id_tokens1
            auto [res_range,res_stag1] = server.search(join_token01,join_token02,id_tokens1,join_token1, join_token2);
            auto k_dec1 = Hash::Get_SHA_256(std::string{K_aes} + std::string{keyword1});
            auto k_dec2 = Hash::Get_SHA_256(std::string{K_aes} + std::string{keyword2});
            
            //计算范围查询的密钥
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
            // std::cout << "k_dec size: " << k_dec.size() << std::endl;

            // 创建输出文件
            // std::ofstream outfile("/home/baum/encdb/JXT/data/jxt_0-2000.csv");
            // if (!outfile.is_open()) {
            //     std::cerr << "Failed to open output file!" << std::endl;
            //     std::exit(1);
            // }

            // 写入 CSV 表头
            // outfile << "table1,table2\n";

            // Step 3.5: Decrypt results
            std::set<std::string> decrypted_range;  // 存储 res_range 解密后的结果
            std::set<std::string> decrypted_stag1;  // 存储 res_stag1 解密后的结果
            // std::set<std::string> decrypted_stag2;  // 存储 res_stag2 解密后的结果


            for (size_t i = 0; i < res_range.size(); ++i) {
                int key_t = i/2;
                const auto& res_i = res_range[i];
                if (i % 2 == 0) {
                    for (const auto& r : res_i) {
                        if (auto decrypted = AESUtil::decrypt(k_dec01[key_t][0], r); decrypted) {
                            decrypted_range.insert(*decrypted);
                            // std::cout << *decrypted << ",\n";
                            // outfile << *decrypted << "," << '\n';  // table1 有值，table2 空
                        }
                    }
                } else {
                    for (const auto& r : res_i) {
                        if (auto decrypted = AESUtil::decrypt(k_dec02[key_t][0], r); decrypted) {
                            decrypted_range.insert(*decrypted);
                            // std::cout << *decrypted << ",\n";
                            // outfile << "," << *decrypted << '\n';  // table1 空，table2 有值
                        }
                    }
                }
            }

            // outfile.close();

            for (size_t i = 0; i < res_stag1.size(); ++i) {
                const auto& res_i = res_stag1[i];
                if (i % 2 == 0) {
                    for (const auto& r : res_i) {
                        if (auto decrypted = AESUtil::decrypt(k_dec1, r); decrypted) {
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

            // // 第一步：求 decrypted_range 和 decrypted_stag1 的交集
            // std::set<std::string> intersection;
            // std::set_intersection(
            //     decrypted_range.begin(), decrypted_range.end(),
            //     decrypted_stag1.begin(), decrypted_stag1.end(),
            //     std::inserter(intersection, intersection.begin())
            // );

            // // 第二步：将交集与 decrypted_stag1求并集
            // std::set<std::string> final_result;
            // std::set_union(
            //     intersection.begin(), intersection.end(),
            //     decrypted_stag2.begin(), decrypted_stag2.end(),
            //     std::inserter(final_result, final_result.begin())
            // );
            // 第一步：求 decrypted_range 和 decrypted_stag1 的交集
            // std::cout << "decrypted_range size: " << decrypted_range.size() << std::endl;
            // std::cout << "decrypted_stag1 size: " << decrypted_stag1.size() << std::endl;
            std::set<std::string> final_result;
            std::set_intersection(
                decrypted_range.begin(), decrypted_range.end(),
                decrypted_stag1.begin(), decrypted_stag1.end(),
                std::inserter(final_result, final_result.begin())
            );
            

            auto search_end = std::chrono::high_resolution_clock::now();
            search_all += std::chrono::duration<double, std::nano>(search_end - search_start).count();
            if (x == 0) std::cout << "res_range size: " << res_range.size() << ", res_stag1 size: " << res_stag1.size() << "\n";
            if (x == 0) std::cout << "final result size: " << final_result.size() << "\n";
        }
        std::cout << "JXT+ average search time: " << search_all / (1000 * 1e6) << " ms\n";
    }
};

int main() {
    JXTp::run();
    return 0;
}