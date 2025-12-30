#include <gtest/gtest.h>
#include "bplus_tdag.h"  // 新BPlusTdag
#include "cube_code.h"
#include "tdag_bf.h"     // TdagBF
#include "TimeUtil.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include "encrypted_db.hpp"  // 假设EMM等外部
#include "tdag_src_3d.hpp"  // 假设
#include "standard_emm.hpp"
#include "coordinate_transformer.hpp"

// CSV加载辅助函数（同原）
std::vector<SpatiotemporalPoint> load_spatiotemporal_data(const std::string& filepath,std::size_t limit_n = SIZE_MAX) {
    std::vector<SpatiotemporalPoint> data;
    std::ifstream file(filepath);
    std::string line;
    std::getline(file, line);  // 表头
    int record_id = 1;
    int counter = 1;
    while (std::getline(file, line)) {
        if (data.size() >= limit_n) break;          // —— 关键：达到目标规模即停止
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> record;
        size_t start = 0, end;
        while ((end = line.find(",", start)) != std::string::npos) {
            record.push_back(line.substr(start, end - start));
            start = end + 1;
        }
        record.push_back(line.substr(start));
        long long ts;
        double lat, lon;
        std::string time_str;
        try {
            time_str = record[7];
            ts = TimeUtil::to_timestamp(time_str);
            lat = std::stod(record[4]);
            lon = std::stod(record[5]);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line " << counter + 2 << ": " << e.what() << std::endl;
            continue;
        }
        data.emplace_back(time_str, ts, lat, lon, record_id++);
        ++counter;
    }
    return data;
}

// todo：形成时间的倒排索引
// 单条数据 → 枚举其所有 TDAG（叶→根，含注入中点）区间并形成时间倒排项
// 输入：一条数据point
// 输出：这条数据对应的所有tdag结点（已有的结点+注入结点）
// 举例子：针对一个B+树上有两个tdag，其高度为4的tdag，tdag1的key对应2012-04-03，
// tdag2的key对应2012-04-04，针对一条数据2012-04-03 对应tdag叶子结点5，record id为12，
// 他的倒排索引为：2012-04-03[5,5]:12; 2012-04-03[4,5]:12; 2012-04-03[4,7]:12;2012-04-03[2,5]:12;2012-04-03[0,7]:12;2012-04-03[4,11]:12;2012-04-03[0,15]:12;
// std::unordered_map<std::string, std::vector<int>> keyword_map time_inverted_index(point.)
// long long day_ts = TimeUtil::date_to_timestamp(point.time_str.substr(0, 10));
// int interval = TimeUtil::time_to_10min_interval(point.time_str);
// auto tdag_bf_ = TdagBF::initialize(8, 0.01, 1000, 0)
// tdag_bf_->descend_tree(leaf, tdag_bf_->range);


class SpatiotemporalDB_Tdag_Test : public ::testing::Test {
protected:
    void SetUp() override {
        // 参数同原
        //  --- 评测构建阶段 (Setup Phase) ---
        auto start_build = std::chrono::high_resolution_clock::now();
        K_token = "89b7a92966f6eb32";
        K_enc = "7975922666f6eb02";
        std::vector<double> min_bounds = {35.51018469, 139.4708776};
        std::vector<double> max_bounds = {35.86715042, 139.9125932};
        int cube_code_level = 8;

        // 初始化BPlusTdag (order=3)
        bplus_tdag = std::make_shared<BPlusTdag>(2, min_bounds, max_bounds, 3);
        

        // 加载数据
        std::string x = "300000"; // 可调整数据集规模
        // std::string path_ = "/nvme/baum/git-project/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        // std::string path_ = "/home/workstation-309/baum/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        std::string path_ = "/home/shijw/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
    std::size_t target_N = 300000;
        all_points_ = load_spatiotemporal_data(path_,target_N);
        ASSERT_FALSE(all_points_.empty());
        std::cout << "Loaded " << all_points_.size() << " data points." << std::endl;

        std::cout << "Loaded " << all_points_.size() << " data points." << std::endl;
        std::cout << "Building B+Tree/SegmentTree/BF index..." << std::endl;

        // 构建索引：per day TdagBF
        std::unordered_map<long long, std::shared_ptr<TdagBF>> day_to_tdag;
        for (const auto& point : all_points_) {
            long long day_ts = TimeUtil::date_to_timestamp(point.time_str.substr(0, 10));
            int interval = TimeUtil::time_to_10min_interval(point.time_str);
            std::vector<double> coords = {point.latitude, point.longitude};
            auto codes = bplus_tdag->getCubeCode()->generateDataCubeCodes(coords);  // 空间关键词

            // 建/得TdagBF
            if (!day_to_tdag.count(day_ts)) {
                auto tdag = TdagBF::initialize(8, 0.01, 1000, 0);  // height=8 for 144
                bplus_tdag->insert(day_ts, tdag);
                day_to_tdag[day_ts] = tdag;
            }
            auto tdag = day_to_tdag[day_ts];
            auto ref = tdag->descend_tree(interval, tdag->range);
            bplus_tdag->update_point(day_ts, interval, codes);  // 插入关键词到TDAG

            // 还有时间的倒排索引，针对tdag结构
            // tdag每个节点都对应一个key，记录每个节点内存的record—id
            for(const auto&[L,R]:ref){
                std::string time_key = "tdag_" + std::to_string(day_ts) + "_" + std::to_string(L) + "-" + std::to_string(R);
                keyword_map[time_key].push_back(point.record_id);
            }
            // 明文倒排（同原，用于ground truth）
            for (const auto& code : codes) {
                keyword_map[code].push_back(point.record_id);
            }
        }
        std::cout << "Index build complete." << std::endl;
        // start_time = "2012-04-03 18:00:00+00";
        // end_time = "2012-04-08 10:10:10+00";
        // lat_min = 40.7;
        // lat_max = 40.76;
        // lon_min = -74.0;
        // lon_max = -73.98;
        start_time = "2012-04-05 01:20:00+00";
        end_time   = "2012-04-05 22:40:00+00";
        lat_min = 35.6798362;
        lat_max = 35.69325604;
        lon_min = 139.6678108;
        lon_max = 139.7722312;
;
        // 4. 初始化加密空间数据库
        emm_engine = std::make_unique<StandardEMM>(K_token, K_enc);
        // db_ = std::make_unique<EncryptedSpatialDB>(std::move(bplus_tree), std::move(emm_engine));
        emm_engine->buildEMM(keyword_map);
        std::cout << "Encrypted index build complete." << std::endl;
        auto end_build = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> build_time = end_build - start_build;
        std::cout << "Build time: " << build_time.count() << " ms\n";

        // --- 评测存储空间 (Storage Overhead) ---
        size_t storage_size_bytes = emm_engine->getStorageSize();
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[Storage] Total Index Size: " << storage_size_bytes / 1024.0 << " KB" << std::endl; 
    }

    // 成员：同原 + bplus_tdag, ccs
    std::shared_ptr<BPlusTdag> bplus_tdag;
    std::vector<SpatiotemporalPoint> all_points_;
    std::unordered_map<std::string, std::vector<int>> keyword_map;
    double lat_min, lat_max, lon_min, lon_max;
    std::string start_time, end_time;
    std::string K_token,K_enc;
    // std::unique_ptr<EncryptedSpatialDB> db_;
    std::unique_ptr<StandardEMM> emm_engine;

};

// 核心测试：时空查询 + 验证cover包含truth (模拟EMM用cover生成时间关键词)
TEST_F(SpatiotemporalDB_Tdag_Test, PerformSpatiotemporalQueryAndVerify) {
    
    std::cout << "\n--- Spatiotemporal Query Test(100 runs) ---" << std::endl;
    double sum_query_gen_ms = 0, sum_eval_ms = 0, sum_decrypt_ms = 0, sum_total_ms = 0;
    std::size_t last_ground_truth = 0, last_returned = 0;

    for (int run = 1; run <= 100; ++run)
    {

        // =================================================================
        // 1. 客户端：准备查询参数
        // =================================================================
        
        QueryTimings timings;

        auto query_search_start = std::chrono::high_resolution_clock::now();

        // 1. 时间换算
        long long start_ts = TimeUtil::to_timestamp(start_time);
        long long end_ts = TimeUtil::to_timestamp(end_time);
        long long start_day_ts = TimeUtil::date_to_timestamp(start_time.substr(0, 10));
        long long end_day_ts = TimeUtil::date_to_timestamp(end_time.substr(0, 10)) + 86399;
        int start_interval = TimeUtil::time_to_10min_interval(start_time);
        int end_interval = TimeUtil::time_to_10min_interval(end_time);

        // 2. 空间CubeCodes
        std::vector<double> query_min = {lat_min, lon_min};
        std::vector<double> query_max = {lat_max, lon_max};
        auto query_codes = bplus_tdag->getCubeCode()->generateQueryCubeCodes(query_min, query_max);

        // std::cout << "Querying from " << start_time << " to " << end_time << std::endl;
        // std::cout << "Querying Lat: [" << lat_min << ", " << lat_max << "], Lon: [" << lon_min << ", " << lon_max << "]" << std::endl;
        // std::cout << "Generated " << query_codes.size() << " spatial query codes." << std::endl;

        // 3. TDAG查询：得TimeCandidate (cover)
        auto time_cands = bplus_tdag->query_time_candidates(start_time, end_time, 
            lat_min, lat_max, lon_min, lon_max);
        auto query_search_end = std::chrono::high_resolution_clock::now();
        timings.query_gen_ms = std::chrono::duration<double, std::milli>(query_search_end - query_search_start).count(); 

        std::vector<std::string> time_tokens;
        for (const auto& cand : time_cands) {
            std::string time_key = "tdag_" + std::to_string(cand.day_ts) + "_" + std::to_string(cand.left_interval) + "-" + std::to_string(cand.right_interval);
            time_tokens.push_back(time_key);
        }
        auto time_base_tokens  = emm_engine->generateTokens(time_tokens);
        
        auto eval_search_start = std::chrono::high_resolution_clock::now();
        EncryptedResult enc_time = emm_engine->query(time_base_tokens);
        auto eval_search_end = std::chrono::high_resolution_clock::now();
        timings.eval_ms = std::chrono::duration<double, std::milli>(eval_search_end - eval_search_start).count();
        

        // 4. 模拟EMM：用cover生成时间关键词，如 "day_left-right"，求交空间
        
        // 假设EMM query(time_tokens, space_keywords) -> ids_time, ids_space (同原)
        // ... (用keyword_map模拟：ids_time from time_tokens, ids_space from space_keywords)
        auto result_search_start = std::chrono::high_resolution_clock::now();
        auto dec_time = emm_engine->decryptResults(enc_time);
        auto result_search_end = std::chrono::high_resolution_clock::now();
        timings.result_decrypt_ms = std::chrono::duration<double, std::milli>(result_search_end - result_search_start).count();

        // ===== 统计 =====
        double total_ms = timings.query_gen_ms + timings.eval_ms + timings.result_decrypt_ms;
        sum_query_gen_ms += timings.query_gen_ms;
        sum_eval_ms      += timings.eval_ms;
        sum_decrypt_ms   += timings.result_decrypt_ms;
        sum_total_ms     += total_ms;

        std::set<int> ids_time(dec_time.begin(), dec_time.end());
    

        // 5. Ground truth & 验证
        std::set<int> ground_truth_ids;
        for (const auto& point : all_points_) {
            if (point.utc_timestamp >= start_ts && point.utc_timestamp <= end_ts &&
                point.latitude >= lat_min && point.latitude <= lat_max &&
                point.longitude >= lon_min && point.longitude <= lon_max) {
                ground_truth_ids.insert(point.record_id);
            }
        }


        // 断言：cover无遗漏 (subset)
        // EXPECT_EQ(ids_time.size(), ground_truth_ids.size());
        // EXPECT_TRUE(std::includes(ids_time.begin(), ids_time.end(),
        //                           ground_truth_ids.begin(), ground_truth_ids.end()))
        //     << "Covers do not fully include ground truth";

        // 假阳允许
        EXPECT_GE(ids_time.size(), ground_truth_ids.size());
        EXPECT_TRUE(std::includes(ids_time.begin(), ids_time.end(),
                                ground_truth_ids.begin(), ground_truth_ids.end()));

        last_ground_truth = ground_truth_ids.size();
        last_returned     = ids_time.size();


        if (run % 10 == 0) {
            std::cout << "[Run " << run << "] latency(ms): gen=" << timings.query_gen_ms
                      << ", eval=" << timings.eval_ms
                      << ", dec="  << timings.result_decrypt_ms
                      << ", total="<< total_ms
                      << " | returned=" << last_returned
                      << ", truth="    << last_ground_truth << "\n";
        }

    }

    //输出100次平均值
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "========================================================\n"; 
    std::cout << "Query Time (Client Token Gen): " << (sum_query_gen_ms / 100.0) << " ms\n";
    std::cout << "Eval Time (Server Evaluation): " << (sum_eval_ms      / 100.0) << " ms\n";
    std::cout << "Result Time (Client Decrypt):  " << (sum_decrypt_ms      / 100.0)<< " ms\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Total Query Latency:           " << (sum_total_ms     / 100.0) << " ms\n";
    std::cout << "Last Returned / Truth    " << last_returned << " / " << last_ground_truth << "\n";
    std::cout << "========================================================\n";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}