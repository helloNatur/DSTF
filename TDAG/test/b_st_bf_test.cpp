#include <gtest/gtest.h>
#include "encrypted_db.hpp"
#include "tdag_src_3d.hpp"
#include "standard_emm.hpp"
#include "coordinate_transformer.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip> // 用于 std::get_time
#include <time.h>  // 用于 timegm, 将UTC时间转换为时间戳
#include <cstdlib>
#include <unordered_map>
#include "bplus_tree.h"
#include "TimeUtil.h"
#include <unordered_set>

void PrintQueryArgs(const std::string& start_time,
                    const std::string& end_time,
                    double lat_min, double lat_max,
                    double lon_min, double lon_max) {
  std::cout << std::fixed << std::setprecision(8);
  std::cout << "start_time = \"" << start_time << "\";\n";
  std::cout << "end_time   = \"" << end_time   << "\";\n";
  std::cout << "lat_min = " << lat_min << ";\n";
  std::cout << "lat_max = " << lat_max << ";\n";
  std::cout << "lon_min = " << lon_min << ";\n";
  std::cout << "lon_max = " << lon_max << ";\n";
}

std::size_t GetEnvSizeT(const char* name, std::size_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        return static_cast<std::size_t>(std::stoull(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

int GetEnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        return std::max(1, std::stoi(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

// CSV加载辅助函数
std::vector<SpatiotemporalPoint> load_spatiotemporal_data(const std::string& filepath, std::size_t limit_n = SIZE_MAX) {
    std::vector<SpatiotemporalPoint> data;
    std::ifstream file(filepath);
    std::string line;
    
    // 跳过表头
    std::getline(file, line); 

    int record_id = 1;
    int counter = 1;
    while (std::getline(file, line)) {
        if (data.size() >= limit_n) break;          // —— 关键：达到目标规模即停止
        // 如果行末尾有回车符，就移除它
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::vector<std::string> record;

        size_t start=0,end;
        while((end=line.find(",",start))!=std::string::npos){
            record.push_back(line.substr(start,end-start));
            start=end+1;
        }
        record.push_back(line.substr(start));

        long long ts;
        double lat, lon;
        std::string time_str;

        try {
            time_str = record[7]; // 假设时间戳在第8列
            ts = TimeUtil::to_timestamp(time_str);
            lat = std::stod(record[4]);  // 假设纬度在第5列
            lon = std::stod(record[5]);  // 假设经度在第6列
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line " << counter + 2 << ": " << e.what() << std::endl;
            continue; // 跳过有问题的行
        }

        // 注意：SpatiotemporalPoint 构造函数需要(long long, double, double, int)
        data.emplace_back(time_str,ts, lat, lon, record_id++);
        ++counter;
    }
    return data;
}

class SpatiotemporalDB_BTree_Test : public ::testing::Test {
protected:
    void SetUp() override {
        // 1. 定义索引的全局参数
        // 纽约市附近的一个大致边界
        //  --- 评测构建阶段 (Setup Phase) ---
        auto start_build = std::chrono::high_resolution_clock::now();
        K_token = "89b7a92966f6eb32";
        K_enc = "7975922666f6eb02";
        //For NYC area
        // std::vector<double> min_bounds = {40.5, -74.2}; 
        // std::vector<double> max_bounds = {41.0, -73.7};

        std::vector<double> min_bounds = {35.51018469, 139.4708776}; 
        std::vector<double> max_bounds = {35.86715042, 139.9125932};
        int cube_code_level = 8; // CubeCode的划分精度

        //初始化B+Tree索引
        bplus_tree = std::make_shared<BPlusTree>(2, min_bounds, max_bounds, cube_code_level);
        

        // 2. 加载真实数据
        std::string x = "300000"; // 这里可以调整为不同的数据集规模
        // std::string path_ = "/home/workstation-309/baum/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        std::string path_ = "/home/shijw/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        std::size_t target_N = GetEnvSizeT("JXT2_LIMIT_N", 300000);
        query_runs = GetEnvInt("JXT2_QUERY_RUNS", 100);
        all_points_ = load_spatiotemporal_data(path_,target_N);
        ASSERT_FALSE(all_points_.empty());

        std::cout << "Loaded " << all_points_.size() << " data points." << std::endl;
        std::cout << "Building B+Tree/SegmentTree/BF index..." << std::endl;

        // 3. 构建索引
        for (const auto& point : all_points_) {
            // a. 解析时间和空间信息
            std::string time_str = point.time_str;
            long long day_timestamp = TimeUtil::date_to_timestamp(time_str.substr(0, 10));
            int interval_idx = TimeUtil::time_to_10min_interval(time_str);
            std::vector<double> coords = {point.latitude, point.longitude};

            // b. 生成该点的空间编码
            // 注意：generateDataCubeCodes返回一个编码向量，因为一个点可能属于不同层级的Cube
            auto codes = bplus_tree->getCubeCode()->generateDataCubeCodes(coords);

            //还需要构建明文倒排索引
            // === 新增：时间戳倒排索引 ===
            std::string time_key = "utctimestamp" + std::to_string(day_timestamp)+ "_" + std::to_string(interval_idx);
            keyword_map[time_key].push_back(point.record_id);
             // === 新增：空间编码倒排索引 ===
            for (const auto& code : codes) {
                keyword_map[code].push_back(point.record_id);
            }
            // c. 更新B+树和对应的段树
            // BPlusTree::update 应该封装了查找或创建段树并更新其BF的逻辑
            auto segment_tree = bplus_tree->search(day_timestamp);
            if (!segment_tree) {
                segment_tree = std::make_shared<SegmentTree>(144, 0.001, 442); // 每天144个10分钟间隔
                bplus_tree->insert(day_timestamp, segment_tree);
            } 
            
            // 生成token（这里简化为SHA-256哈希，实际应用中应使用安全的加密token）
            std::string token_input = std::string{K_token} + time_key ;
            std::shared_ptr<std::vector<unsigned char>> token =
                std::make_shared<std::vector<unsigned char>>(Hash::Get_SHA_256(token_input));   
            bplus_tree->update(day_timestamp, interval_idx, interval_idx, token, codes);
        }
        std::cout << "Index build complete." << std::endl;
        start_time = "2012-04-05 01:20:00+00";
        end_time   = "2012-04-05 22:40:00+00";
        lat_min = 35.6798362;
        lat_max = 35.69325604;
        lon_min = 139.6678108;
        lon_max = 139.7722312
;

        PrintQueryArgs(start_time, end_time,
               lat_min, lat_max, lon_min, lon_max);

        //tdag换成BPlusTree
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

    std::shared_ptr<BPlusTree> bplus_tree;
    KeywordMap keyword_map;
    std::vector<SpatiotemporalPoint> all_points_;
    std::string start_time, end_time;
    double lat_min, lat_max, lon_min, lon_max;
    std::string K_token,K_enc;
    int query_runs = 100;
    // std::unique_ptr<EncryptedSpatialDB> db_;
    std::unique_ptr<StandardEMM> emm_engine;

};


TEST_F(SpatiotemporalDB_BTree_Test, PerformSpatiotemporalQueryAndVerify) {
    std::cout << "\n--- Spatiotemporal Query Test(" << query_runs << " runs) ---\n" << std::endl;

    double sum_query_gen_ms = 0, sum_eval_ms = 0, sum_decrypt_ms = 0, sum_total_ms = 0;
    std::size_t last_ground_truth = 0, last_returned = 0;

    int print_every = std::max(1, query_runs / 10);
    for (int run = 1; run <= query_runs; ++run)
    {
        // =================================================================
        // 1. 客户端：准备查询参数
        // =================================================================
        
        QueryTimings timings;

        // Query Time: 生成查询令牌
        auto query_search_start = std::chrono::high_resolution_clock::now();
        // a. 解析时间范围
        long long start_ts = TimeUtil::to_timestamp(start_time);
        long long end_ts = TimeUtil::to_timestamp(end_time);
        long long start_day_ts = TimeUtil::date_to_timestamp(start_time.substr(0, 10));
        long long end_day_ts = TimeUtil::date_to_timestamp(end_time.substr(0, 10)) + 86399;
        int start_interval = TimeUtil::time_to_10min_interval(start_time);
        int end_interval = TimeUtil::time_to_10min_interval(end_time);

        // b. 生成空间查询编码
        std::vector<double> query_min_coords = {lat_min, lon_min};
        std::vector<double> query_max_coords = {lat_max, lon_max};
        auto query_codes = bplus_tree->getCubeCode()->generateQueryCubeCodes(query_min_coords, query_max_coords);

        // std::cout << "Querying from " << start_time << " to " << end_time << std::endl;
        // std::cout << "Querying Lat: [" << lat_min << ", " << lat_max << "], Lon: [" << lon_min << ", " << lon_max << "]" << std::endl;
        // std::cout << "Generated " << query_codes.size() << " spatial query codes." << std::endl;

         //返回时间+空间的候选token，但不直接查询emm
        std::vector<SegmentTree::IntervalResult> time_tokens1;
        time_tokens1 = bplus_tree->query_sql(start_time, end_time, lat_min, lat_max, lon_min, lon_max);

        //为空间维度生成token
        // std::vector<std::string> space_keywords = query_codes;
        // auto space_tokens = emm_engine->generateTokens(space_keywords);
        auto query_search_end = std::chrono::high_resolution_clock::now();
        timings.query_gen_ms = std::chrono::duration<double, std::milli>(query_search_end - query_search_start).count(); 

        std::vector<std::shared_ptr<std::vector<unsigned char>>> t_tokens;
        t_tokens.reserve( // 预估容量，降低realloc次数
            [&]{
                size_t s = 0;
                for (const auto& r : time_tokens1) s += r.tokens.size();
                return s;
            }()
        );

        // （可选）跨区间去重：把指针指向的数据内容当作字节串去重
        std::unordered_set<std::string> seen;
        seen.reserve(t_tokens.capacity() * 2);

        for (const auto& interval_result : time_tokens1) {
            for (const auto& tok_ptr : interval_result.tokens) {
                if (!tok_ptr || tok_ptr->empty()) continue;

                // 用内容去重（把字节序列临时转为string做key）
                const std::string key(reinterpret_cast<const char*>(tok_ptr->data()),
                                    tok_ptr->size());
                if (seen.insert(key).second) {
                    t_tokens.push_back(tok_ptr);
                }
            }
        }

        // --- FIX 1: 创建正确类型的向量 ---
        std::vector<Ciphertext> time_tokens; // Ciphertext 是 std::vector<unsigned char>
        time_tokens.reserve(t_tokens.size()); // 预分配内存以提高效率
        for (const auto& ptr : t_tokens) {
            time_tokens.push_back(*ptr); // 解引用智能指针，并将实际的vector<unsigned char>复制进去
        }

        // Eval Time: 服务器评估
        auto eval_search_start = std::chrono::high_resolution_clock::now();
        EncryptedResult enc_time = emm_engine->query(time_tokens);
        // EncryptedResult enc_space = emm_engine->query(space_tokens);
        auto eval_search_end = std::chrono::high_resolution_clock::now();
        timings.eval_ms = std::chrono::duration<double, std::milli>(eval_search_end - eval_search_start).count();

        // Result Time: 客户端解密
        auto result_search_start = std::chrono::high_resolution_clock::now();
        auto ids_time = emm_engine->decryptResults(enc_time);
        // auto ids_space = emm_engine->decryptResults(enc_space);

        // 维度内已是并集结果；现在做跨维度交集（AND）
        // std::set<int> set_time(ids_time.begin(), ids_time.end());
        // std::set<int> set_space(ids_space.begin(), ids_space.end());

        // std::set<int> decrypted_and_ids;
        // std::set_intersection(set_time.begin(), set_time.end(),
        //                     set_space.begin(), set_space.end(),
        //                     std::inserter(decrypted_and_ids, decrypted_and_ids.begin()));
        auto result_search_end = std::chrono::high_resolution_clock::now();
        timings.result_decrypt_ms = std::chrono::duration<double, std::milli>(result_search_end - result_search_start).count();
        

        // ===== 统计 =====
        double total_ms = timings.query_gen_ms + timings.eval_ms + timings.result_decrypt_ms;
        sum_query_gen_ms += timings.query_gen_ms;
        sum_eval_ms      += timings.eval_ms;
        sum_decrypt_ms   += timings.result_decrypt_ms;
        sum_total_ms     += total_ms;

        
        std::set<int> set_time(ids_time.begin(), ids_time.end());



        // =================================================================
        // 3. (测试用) 精确过滤与验证
        // =================================================================
        std::set<int> retrieved_ids;
        // 收集候选的day_timestamp
        std::set<long long> candidate_day_timestamps;
        for (const auto& interval_result : time_tokens1) {
            candidate_day_timestamps.insert(interval_result.day_ts);
        }
        // 模拟服务器在获取候选数据块后进行的精确过滤
        for (const auto& point : all_points_) {
            long long point_day_ts = TimeUtil::date_to_timestamp(point.time_str);
            
            // 只检查那些被索引标记为候选的数据
            if (candidate_day_timestamps.count(point_day_ts)) {
                // 进行精确的时空范围检查
                if (point.utc_timestamp >= start_ts && point.utc_timestamp <= end_ts &&
                    point.latitude >= lat_min && point.latitude <= lat_max &&
                    point.longitude >= lon_min && point.longitude <= lon_max) {
                    retrieved_ids.insert(point.record_id);
                }
            }
        }
        
        // =================================================================
        // 4. (测试用) 计算基准真相 (Ground Truth)
        // =================================================================
        std::set<int> ground_truth_ids;
        for (const auto& point : all_points_) {
            if (point.utc_timestamp >= start_ts && point.utc_timestamp <= end_ts &&
                point.latitude >= lat_min && point.latitude <= lat_max &&
                point.longitude >= lon_min && point.longitude <= lon_max) {
                ground_truth_ids.insert(point.record_id);
            }
        }

        // std::cout << "Final retrieved results after filtering: " << retrieved_ids.size() << std::endl;
        // std::cout << "Ground truth results: " << ground_truth_ids.size() << std::endl;

        // 断言：最终精确过滤后的结果必须与基准真相完全一致
        // 这证明了我们的索引没有漏掉任何结果 (没有False Negatives)
        // 判定：无漏召回（FN=0）
        ASSERT_EQ(retrieved_ids.size(), ground_truth_ids.size());
        ASSERT_EQ(retrieved_ids, ground_truth_ids);


        // 和 ground truth 对比（允许假阳性，但不能漏掉真值）
        EXPECT_GE(set_time.size(), ground_truth_ids.size());

        // ground_truth_ids ⊆ decrypted_and_ids//set_time
        EXPECT_TRUE(std::includes(
            set_time.begin(), set_time.end(),
            ground_truth_ids.begin(), ground_truth_ids.end()
        )) << "decrypted_and_ids does not fully include ground_truth_ids";

        last_ground_truth = ground_truth_ids.size();
        last_returned     = set_time.size();

        if (run % print_every == 0) {
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
    std::cout << "Query Time (Client Token Gen): " << (sum_query_gen_ms / query_runs) << " ms\n";
    std::cout << "Eval Time (Server Evaluation): " << (sum_eval_ms      / query_runs) << " ms\n";
    std::cout << "Result Time (Client Decrypt):  " << (sum_decrypt_ms      / query_runs)<< " ms\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Total Query Latency:           " << (sum_total_ms     / query_runs) << " ms\n";
    std::cout << "Last Returned / Truth    " << last_returned << " / " << last_ground_truth << "\n";
    std::cout << "========================================================\n";
    
    
}
