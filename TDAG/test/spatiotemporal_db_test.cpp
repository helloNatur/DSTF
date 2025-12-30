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
#include "TimeUtil.h"

// 原始时空点 -> (CoordinateTransformer) -> 3D整数网格点 -> 
// (TdagSRC3D) -> 关键词-ID映射 -> (StandardEMM) -> 加密索引


#include <iostream>
#include <iomanip>
#include <string>
#include <ctime>
#include <cstdint>


// 将 epoch（秒或毫秒）转成 "YYYY-MM-DD HH:MM:SS+00"
static std::string epoch_to_iso8601_utc(int64_t ts) {
  // 粗略判断是否毫秒级（> ~ 10^11 就当作毫秒）
  if (ts > 100000000000LL) ts /= 1000;
  std::time_t t = static_cast<std::time_t>(ts);

  // 用 gmtime 转 UTC，再格式化
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &t);
#else
  std::tm* tmp = std::gmtime(&t);
  if (tmp) tm_utc = *tmp;
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm_utc, "%Y-%m-%d %H:%M:%S") << "+00";
  return oss.str();
}

static void PrintQueryBoxAsInit(const SpatiotemporalQueryBox& q) {
  std::cout << std::fixed << std::setprecision(8);
  std::cout << "SpatiotemporalQueryBox query_box = {\n";
  std::cout << "            .min_ts = TimeUtil::to_timestamp(\""
            << epoch_to_iso8601_utc(q.min_ts) << "\"),\n";
  std::cout << "            .max_ts = TimeUtil::to_timestamp(\""
            << epoch_to_iso8601_utc(q.max_ts) << "\"),\n";
  std::cout << "            .min_lat = " << q.min_lat << ",\n";
  std::cout << "            .max_lat = " << q.max_lat << ",\n";
  std::cout << "            .min_lon = " << q.min_lon << ",\n";
  std::cout << "            .max_lon = " << q.max_lon << "\n";
  std::cout << "        };\n";
}


// CSV加载辅助函数
std::vector<SpatiotemporalPoint> load_spatiotemporal_data(const std::string& filepath,std::size_t limit_n = SIZE_MAX) {
    std::vector<SpatiotemporalPoint> data;
    std::ifstream file(filepath);
    std::string line;
    
    // 跳过表头
    std::getline(file, line); 

    int record_id = 1;
    int counter = 1;
    while (std::getline(file, line)) {
        if (data.size() >= limit_n) break; // 如果行末尾有回车符，就移除它
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
            std::string time_str = record[7];
            ts = TimeUtil::to_timestamp(time_str);
            lat = std::stod(record[4]);  // 假设纬度在第5列
            lon = std::stod(record[5]);  // 假设经度在第6列
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line " << counter + 2 << ": " << e.what() << std::endl;
            continue; // 跳过有问题的行
        }

        data.emplace_back(time_str, ts, lat, lon, record_id++);
        ++counter;
    }
    return data;
}


// ===================================================================================
// =================== 1. 功能正确性测试 (Functional Correctness) ==================
// ===================================================================================

class SpatiotemporalDB_Test : public ::testing::Test {
protected:
    void SetUp() override {
        // 1. 加载真实数据
        //  --- 评测构建阶段 (Setup Phase) ---
        auto start_build = std::chrono::high_resolution_clock::now();
        std::string x = "300000"; // 这里可以调整为不同的数据集规模
        // std::string path_ = "/home/workstation-309/baum/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        std::string path_ = "/home/shijw/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        std::size_t target_N = 300000;
        all_points_ = load_spatiotemporal_data(path_,target_N);
        ASSERT_FALSE(all_points_.empty());

        std::cout << "Loaded " << all_points_.size() << " data points." << std::endl;
        // 2. 分析数据范围以初始化转换器
        auto [min_it, max_it] = std::minmax_element(all_points_.begin(), all_points_.end(), 
            [](const auto& a, const auto& b){ return a.utc_timestamp < b.utc_timestamp; });
        long long min_ts = min_it->utc_timestamp;
        long long max_ts = max_it->utc_timestamp;

        
        // 将真实的、连续的经纬度和时间坐标转换到这个离散网格上。
        const unsigned int GRID_BITS = 7;
        transformer_ = std::make_unique<CoordinateTransformer>(min_ts, max_ts, GRID_BITS);

        // 3. 转换数据为 EMM 所需的格式，倒排索引
        PointMap3D point_map_for_emm;
        for (const auto& st_point : all_points_) {
            GridPoint3D grid_point = transformer_->to_grid_point(st_point);
            point_map_for_emm[grid_point].push_back(st_point.record_id);
        }

        // 4. 初始化加密数据库
        // 负责在这个离散网格上构建用于加密和查询的索引结构
        const int MAX_GRID_COORD = 1 << GRID_BITS;
        auto tdag_scheme = std::make_unique<TdagSRC3D>(MAX_GRID_COORD, MAX_GRID_COORD, MAX_GRID_COORD);
        auto emm_engine = std::make_unique<StandardEMM>("89b7a92966f6eb32", "7975922666f6eb02");
        db_ = std::make_unique<EncryptedSpatialDB>(std::move(tdag_scheme), std::move(emm_engine));
        
        // 5. 构建索引
        db_->build(point_map_for_emm);

        auto end_build = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> build_time = end_build - start_build;
        std::cout << "Build time: " << build_time.count() << " ms\n";

        // 4. --- 评测存储空间 (Storage Overhead) ---
        size_t storage_size_bytes = db_->getStorageSize();

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[Storage] Total Index Size: " << storage_size_bytes / 1024.0 << " KB" << std::endl;

        query_box = {
            .min_ts = TimeUtil::to_timestamp("2012-04-05 01:20:00+00"),
            .max_ts = TimeUtil::to_timestamp("2012-04-05 22:40:00+00"),
            .min_lat = 35.6798362,
            .max_lat = 35.69325604,
            .min_lon = 139.6678108,
            .max_lon = 139.7722312













        };

        PrintQueryBoxAsInit(query_box);
    }

    std::unique_ptr<EncryptedSpatialDB> db_;
    std::unique_ptr<CoordinateTransformer> transformer_;
    std::vector<SpatiotemporalPoint> all_points_;
    SpatiotemporalQueryBox query_box;
};


// TEST_F(SpatiotemporalDB_Test, PerformSpatiotemporalQuery0) {
//     std::cout << "\n--- Spatiotemporal Query Test ---" << std::endl;

//     // --- 使用指定的查询范围 ---
//     // 1. 定义一个真实世界的时空查询范围
//     SpatiotemporalQueryBox query_box = {
//         .min_ts = TimeUtil::to_timestamp("2012-04-03 18:00:00"),
//         .max_ts = TimeUtil::to_timestamp("2012-04-08 10:10:10"),
//         .min_lat = 40.7,
//         .max_lat = 40.76,
//         .min_lon = -74.0,
//         .max_lon = -73.98
//     };

//     // 2. 将真实查询范围转换为整数网格范围
//     Rect3D grid_rect = transformer_->to_grid_rect(query_box);

//     std::cout << "Querying time range [" << query_box.min_ts << ", " << query_box.max_ts << "]" << std::endl;
//     std::cout << "Querying geo box [(" << query_box.min_lat << "," << query_box.min_lon 
//               << "), (" << query_box.max_lat << "," << query_box.max_lon << ")]" << std::endl;

//     // 3. 执行查询 (注意：返回的是超集)
//     auto results_superset_ids = db_->query(grid_rect);
//     std::set<int> results_set(results_superset_ids.begin(), results_superset_ids.end());
//     std::cout << "Superset query returned " << results_set.size() << " results." << std::endl;

//     // 4. 在客户端计算精确结果以进行验证
//     std::set<int> expected_exact_ids;
//     for (const auto& p : all_points_) {
//         if (p.utc_timestamp >= query_box.min_ts && p.utc_timestamp <= query_box.max_ts &&
//             p.latitude >= query_box.min_lat && p.latitude <= query_box.max_lat &&
//             p.longitude >= query_box.min_lon && p.longitude <= query_box.max_lon) {
//             expected_exact_ids.insert(p.record_id);
//         }
//     }
//     std::cout << "Expected exact results count: " << expected_exact_ids.size() << std::endl;

//     // 5. 验证: 返回的结果集必须包含所有精确结果
//     bool all_found = true;
//     for (int expected_id : expected_exact_ids) {
//         if (results_set.find(expected_id) == results_set.end()) {
//             all_found = false;
//             std::cerr << "Validation Error: Expected record ID " << expected_id << " not in result set!" << std::endl;
//         }
//     }
    
//     // 断言：精确结果不应为空，以证明查询有效
//     EXPECT_FALSE(expected_exact_ids.empty()) << "Query returned no results, check query box or dataset.";
//     // 断言：返回的超集必须包含所有精确结果
//     EXPECT_TRUE(all_found);
//     // 断言：返回的超集大小必须大于或等于精确结果大小
//     EXPECT_GE(results_set.size(), expected_exact_ids.size());
// }

// TEST_F(SpatiotemporalDB_Test, DataDrivenSpatiotemporalQuery) {
//     std::cout << "\n--- Data-Driven Spatiotemporal Query Test ---" << std::endl;

//     // 1. 从数据集中选择一个已知的点作为查询中心
//     //    选择中间的点可以避免边界问题，使测试更具代表性
//     const SpatiotemporalPoint& center_point = all_points_[all_points_.size() / 2];
//     std::cout << "Chosen center point for query: ID=" << center_point.record_id 
//               << ", TS=" << center_point.utc_timestamp 
//               << ", Lat=" << center_point.latitude 
//               << ", Lon=" << center_point.longitude << std::endl;

//     // 2. 围绕这个已知点构建一个小的、保证非空的查询范围
//     SpatiotemporalQueryBox query_box = {
//         .min_ts = center_point.utc_timestamp - 3600,       // 前后一小时
//         .max_ts = center_point.utc_timestamp + 3600,
//         .min_lat = center_point.latitude - 0.05,           // 周围约5公里的地理范围
//         .max_lat = center_point.latitude + 0.05,
//         .min_lon = center_point.longitude - 0.05,
//         .max_lon = center_point.longitude + 0.05
//     };

//     // 3. 将真实查询范围转换为整数网格范围
//     Rect3D grid_rect = transformer_->to_grid_rect(query_box);

//     // 4. 执行查询 (返回的是超集)
//     auto results_superset_ids = db_->query(grid_rect);
//     std::set<int> results_set(results_superset_ids.begin(), results_superset_ids.end());
//     std::cout << "Superset query returned " << results_set.size() << " results." << std::endl;

//     // 5. 在客户端计算精确结果以进行验证
//     std::set<int> expected_exact_ids;
//     for (const auto& p : all_points_) {
//         if (p.utc_timestamp >= query_box.min_ts && p.utc_timestamp <= query_box.max_ts &&
//             p.latitude >= query_box.min_lat && p.latitude <= query_box.max_lat &&
//             p.longitude >= query_box.min_lon && p.longitude <= query_box.max_lon) {
//             expected_exact_ids.insert(p.record_id);
//         }
//     }
//     std::cout << "Expected exact results count: " << expected_exact_ids.size() << std::endl;
    
//     // 6. 验证
//     //    首先，我们断言期望的结果集不应为空，因为我们是围绕一个真实存在的点构建的查询
//     ASSERT_FALSE(expected_exact_ids.empty()) << "The data-driven query box should have found at least one point.";
    
//     //    其次，验证返回的超集结果必须包含所有精确结果
//     bool all_found = true;
//     for (int expected_id : expected_exact_ids) {
//         if (results_set.find(expected_id) == results_set.end()) {
//             all_found = false;
//             std::cerr << "Validation Error: Expected record ID " << expected_id << " not in result set!" << std::endl;
//         }
//     }
    
//     EXPECT_TRUE(all_found) << "The query result is missing one or more expected record IDs.";
//     EXPECT_GE(results_set.size(), expected_exact_ids.size());
// }

// =================================================================================
// <<< 新增的性能测试用例
// =================================================================================
TEST_F(SpatiotemporalDB_Test, PerformanceBenchmark) {
    // // --- 1. 构建时间和存储开销测试 ---
    // std::cout << "--- [1] Construction & Storage Overhead ---\n";
    // TimeUtil timer;
    // timer.start();
    // db_->buildIndex(all_points_);
    // timer.stop();
    
    // double construction_time_ms = timer.get_duration_ms();
    // size_t storage_size_bytes = db_->getStorageSize();

    // std::cout << "Construction Time: " << construction_time_ms << " ms\n";
    // std::cout << "Storage Overhead: " << storage_size_bytes << " bytes (" 
    //           << static_cast<double>(storage_size_bytes) / 1024.0 << " KB)\n\n";

    // --- 2. 查询性能测试 ---
    std::cout << "\n--- Spatiotemporal Query Test(100 runs) ---\n" << std::endl;

    double sum_query_gen_ms = 0, sum_eval_ms = 0, sum_decrypt_ms = 0, sum_total_ms = 0;
    std::size_t last_ground_truth = 0, last_returned = 0;

    for (int run = 1; run <= 100; ++run)
    {

        RangeQueryResult rangequery;

        // 2. 将真实查询范围转换为整数网格范围
        Rect3D grid_rect = transformer_->to_grid_rect(query_box);

        // std::cout << "Querying time range [" << query_box.min_ts << ", " << query_box.max_ts << "]" << std::endl;
        // std::cout << "Querying geo box [(" << query_box.min_lat << "," << query_box.min_lon 
        //         << "), (" << query_box.max_lat << "," << query_box.max_lon << ")]" << std::endl;

        // 3. 执行查询 (注意：返回的是超集)
        
        rangequery = db_->benchmarkRangeQuery(grid_rect);

         // ===== 统计 =====
        double total_ms = rangequery.timings.query_gen_ms + rangequery.timings.eval_ms + rangequery.timings.result_decrypt_ms;
        sum_query_gen_ms += rangequery.timings.query_gen_ms;
        sum_eval_ms      += rangequery.timings.eval_ms;
        sum_decrypt_ms   += rangequery.timings.result_decrypt_ms;
        sum_total_ms     += total_ms;


        std::set<int> set_time(rangequery.set_times.begin(), rangequery.set_times.end());
        // 可选：验证结果的正确性
        // 4. 在客户端计算精确结果以进行验证
        std::set<int> ground_truth_ids;
        for (const auto& p : all_points_) {
            if (p.utc_timestamp >= query_box.min_ts && p.utc_timestamp <= query_box.max_ts &&
                p.latitude >= query_box.min_lat && p.latitude <= query_box.max_lat &&
                p.longitude >= query_box.min_lon && p.longitude <= query_box.max_lon) {
                ground_truth_ids.insert(p.record_id);
            }
        }

        // 6. 验证
         // 和 ground truth 对比（允许假阳性，但不能漏掉真值）
        EXPECT_GE(set_time.size(), ground_truth_ids.size());
        
        //    其次，验证返回的超集结果必须包含所有精确结果
        EXPECT_TRUE(std::includes(
            set_time.begin(), set_time.end(),
            ground_truth_ids.begin(), ground_truth_ids.end()
        )) << "decrypted_and_ids does not fully include ground_truth_ids";


        last_ground_truth = ground_truth_ids.size();
        last_returned     = set_time.size();

        if (run % 10 == 0) {
            std::cout << "[Run " << run << "] latency(ms): gen=" << rangequery.timings.query_gen_ms
                      << ", eval=" << rangequery.timings.eval_ms
                      << ", dec="  << rangequery.timings.result_decrypt_ms
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


// ===================================================================================
// =================== 2. 性能基准测试 (Performance Benchmark) =======================
// ===================================================================================

// // 定义基准测试的参数结构体
// struct BenchmarkParams {
//     size_t dataset_size;        // 要使用的数据库中的条目数
//     double query_selectivity;   // 查询范围的选择性 (占整个数据集范围的百分比)
//     unsigned int grid_bits;     // 网格精度
// };

// class SpatiotemporalDB_Benchmark : public ::testing::TestWithParam<BenchmarkParams> {
// protected:
//     void SetUp() override {
//         // 加载完整数据集
//         std::string x = "15000"; // 这里可以调整为不同的数据集规模
//         std::string path_ = "/nvme/baum/git-project/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
//         all_points_ = load_spatiotemporal_data(path_);
//         ASSERT_FALSE(all_points_.empty());
//     }

//     std::vector<SpatiotemporalPoint> all_points_;
// };

// TEST_P(SpatiotemporalDB_Benchmark, PerformanceProfile) {
//     auto params = GetParam();
//     std::cout << "\n==========================================================\n"
//               << "BENCHMARKING with: "
//               << "Dataset Size = " << params.dataset_size
//               << ", Query Selectivity = " << params.query_selectivity * 100 << "%"
//               << ", Grid Bits = " << params.grid_bits
//               << "\n----------------------------------------------------------" << std::endl;

//     // 1. 准备数据子集
//     std::vector<SpatiotemporalPoint> data_subset(all_points_.begin(), all_points_.begin() + std::min(params.dataset_size, all_points_.size()));
    
//     auto [min_it, max_it] = std::minmax_element(data_subset.begin(), data_subset.end(), 
//         [](const auto& a, const auto& b){ return a.utc_timestamp < b.utc_timestamp; });
//     long long min_ts = min_it->utc_timestamp;
//     long long max_ts = max_it->utc_timestamp;

//     auto transformer = std::make_unique<CoordinateTransformer>(min_ts, max_ts, params.grid_bits);

//     PointMap3D point_map;
//     for (const auto& st_point : data_subset) {
//         GridPoint3D grid_point = transformer->to_grid_point(st_point);
//         point_map[grid_point].push_back(st_point.record_id);
//     }
    
//     // 2. 初始化加密方案
//     const int MAX_GRID_COORD = 1 << params.grid_bits;
//     auto tdag_scheme = std::make_unique<TdagSRC3D>(MAX_GRID_COORD, MAX_GRID_COORD, MAX_GRID_COORD);
//     auto emm_engine = std::make_unique<StandardEMM>("benchmark_token_key", "benchmark_enc_key");
//     auto db = std::make_unique<EncryptedSpatialDB>(std::move(tdag_scheme), std::move(emm_engine));

//     // 3. --- 评测构建阶段 (Setup Phase) ---
//     auto start_build = std::chrono::high_resolution_clock::now();
//     db->build(point_map);
//     auto end_build = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double, std::milli> build_time = end_build - start_build;

//     // 4. --- 评测存储空间 (Storage Overhead) ---
//     size_t storage_size_bytes = db->getStorageSize();

//     std::cout << std::fixed << std::setprecision(2);
//     std::cout << "[Setup] Build Time: " << build_time.count() << " ms" << std::endl;
//     std::cout << "[Storage] Total Index Size: " << storage_size_bytes / 1024.0 << " KB" << std::endl;
//     std::cout << "[Storage] Bytes per Point: " << static_cast<double>(storage_size_bytes) / data_subset.size() << " bytes/item" << std::endl;

//     // 5. --- 评测查询阶段 (Query Phase) ---
//     // 构建一个具有特定选择性的查询
//     long long ts_range = max_ts - min_ts;
//     double lat_range = 180.0;
//     double lon_range = 360.0;
//     double range_multiplier = std::cbrt(params.query_selectivity); // 立方根以分配到三个维度

//     SpatiotemporalQueryBox query_box = {
//         .min_ts = min_ts, 
//         .max_ts = min_ts + static_cast<long long>(ts_range * range_multiplier),
//         .min_lat = -90.0, 
//         .max_lat = -90.0 + lat_range * range_multiplier,
//         .min_lon = -180.0, 
//         .max_lon = -180.0 + lon_range * range_multiplier
//     };
    
//     GridRect3D grid_rect = transformer->to_grid_rect(query_box);

//     auto start_query = std::chrono::high_resolution_clock::now();
//     std::vector<RecordID> results_superset = db->query(grid_rect);
//     auto end_query = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double, std::milli> query_time = end_query - start_query;

//     std::cout << "[Query] Search Time: " << query_time.count() << " ms" << std::endl;
//     std::cout << "[Query] Results Returned (Superset): " << results_superset.size() << " items" << std::endl;
//     std::cout << "==========================================================" << std::endl;
// }

// // 实例化基准测试套件，提供多组参数
// INSTANTIATE_TEST_SUITE_P(PerformanceBenchmarks,
//                          SpatiotemporalDB_Benchmark,
//                          ::testing::Values(
//                              // 测试不同数据规模 (固定选择性和精度)
//                              BenchmarkParams{5000, 0.01, 14},
//                              BenchmarkParams{10000, 0.01, 14},
//                              BenchmarkParams{15000, 0.01, 14},
//                              // 测试不同查询选择性 (固定规模和精度)
//                              BenchmarkParams{10000, 0.001, 14}, // 0.1%
//                              BenchmarkParams{10000, 0.1, 14},  // 10%
//                              // 测试不同网格精度 (固定规模和选择性)
//                              BenchmarkParams{10000, 0.01, 12},
//                              BenchmarkParams{10000, 0.01, 16}
//                          ));