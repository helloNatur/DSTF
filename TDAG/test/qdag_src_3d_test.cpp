#include <gtest/gtest.h>
#include "encrypted_db.hpp"
#include "qdag_src_3d.hpp" // 引入新方案
#include "standard_emm.hpp"
#include "coordinate_transformer.hpp"
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>


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


std::vector<SpatiotemporalPoint> load_spatiotemporal_data(const std::string& filepath,std::size_t limit_n = SIZE_MAX) {
    std::vector<SpatiotemporalPoint> data;
    std::ifstream file(filepath);
    std::string line;
    
    // 跳过表头
    std::getline(file, line); 

    int record_id = 1;
    int counter = 1;
    while (std::getline(file, line)) {
        // 如果行末尾有回车符，就移除它
        if (data.size() >= limit_n) break; 
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

class QdagSrc3dTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 1. 加载真实数据
        //  --- 评测构建阶段 (Setup Phase) ---
        auto start_build = std::chrono::high_resolution_clock::now();
        K_token = "89b7a92966f6eb32";
        K_enc = "7975922666f6eb02";
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
        qdag_scheme_ = std::make_shared<QdagSRC3D>(MAX_GRID_COORD, MAX_GRID_COORD, MAX_GRID_COORD);
        emm_engine_ = std::make_shared<StandardEMM>(K_token, K_enc);
        db_ = std::make_unique<EncryptedSpatialDB>(qdag_scheme_, emm_engine_);

        // 5. 构建索引
        db_->build(point_map_for_emm);

        auto end_build = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> build_time = end_build - start_build;
        std::cout << "Build time: " << build_time.count() << " ms\n";
        //  --- 评测存储空间 (Storage Overhead) ---
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
    std::string K_token,K_enc;
    std::unique_ptr<CoordinateTransformer> transformer_;
    std::vector<SpatiotemporalPoint> all_points_;
    std::shared_ptr<Index_Interface> qdag_scheme_;
    std::shared_ptr<EMM_Interface> emm_engine_;
    SpatiotemporalQueryBox query_box;
};




// =================================================================================
// <<< 新增的性能测试用例
// =================================================================================
TEST_F(QdagSrc3dTest, PerformanceBenchmark) {
    // std::cout << std::fixed << std::setprecision(2);
    // std::cout << "========================================================\n";
    // std::cout << "           PERFORMANCE BENCHMARK RESULTS\n";
    // std::cout << "========================================================\n";
    // // std::cout << "Dataset size: " << all_points_.size() << " records\n";
    // // std::cout << "Grid dimensions: " << (1 << GRID_BITS) << "x" << (1 << GRID_BITS) << "x" << (1 << GRID_BITS) << "\n";
    // std::cout << "--------------------------------------------------------\n\n";

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
        QueryTimings timings;
        // 2.1. Query Time: 生成查询令牌
        auto query_search_start = std::chrono::high_resolution_clock::now();
        // SpatiotemporalQueryBox query_box = {
        //     .min_ts = TimeUtil::to_timestamp("2012-04-03 18:00:00"),
        //     .max_ts = TimeUtil::to_timestamp("2012-04-08 10:10:10"),
        //     .min_lat = 40.7,
        //     .max_lat = 40.76,
        //     .min_lon = -74.0,
        //     .max_lon = -73.98
        // };
        // SpatiotemporalQueryBox query_box = {
        //     .min_ts = TimeUtil::to_timestamp("2012-04-04 02:00:09+08"),
        //     .max_ts = TimeUtil::to_timestamp("2012-04-04 02:48:57+08"),
        //     .min_lat = 40.7,
        //     .max_lat = 40.76,
        //     .min_lon = -74.0,
        //     .max_lon = -73.98
        // };
        // SpatiotemporalQueryBox query_box = {
        //     .min_ts = TimeUtil::to_timestamp("2012-04-03 00:00:00+00"),
        //     .max_ts = TimeUtil::to_timestamp("2012-04-09 00:00:00+00"),
        //     .min_lat = 35.66318885,
        //     .max_lat = 35.6985962,
        //     .min_lon = 139.7002792,
        //     .max_lon = 139.7504282
        // };

        // 2. 将真实查询范围转换为整数网格范围
        Rect3D grid_rect = transformer_->to_grid_rect(query_box);

        // std::cout << "Querying time range [" << query_box.min_ts << ", " << query_box.max_ts << "]" << std::endl;
        // std::cout << "Querying geo box [(" << query_box.min_lat << "," << query_box.min_lon 
        //         << "), (" << query_box.max_lat << "," << query_box.max_lon << ")]" << std::endl;


        auto query_labels = qdag_scheme_->getQueryLabels(grid_rect);
        auto search_tokens = emm_engine_->generateTokens(query_labels);
        auto query_search_end = std::chrono::high_resolution_clock::now();
        timings.query_gen_ms = std::chrono::duration<double, std::milli>(query_search_end - query_search_start).count();

        // 2. Eval Time: 服务器评估
        auto eval_search_start = std::chrono::high_resolution_clock::now();
        auto encrypted_results = emm_engine_->query(search_tokens);
        auto eval_search_end = std::chrono::high_resolution_clock::now();
        timings.eval_ms = std::chrono::duration<double, std::milli>(eval_search_end - eval_search_start).count();

        // 3. Result Time: 客户端解密
        auto result_search_start = std::chrono::high_resolution_clock::now();
        auto decrypted_ids = emm_engine_->decryptResults(encrypted_results);
        auto result_search_end = std::chrono::high_resolution_clock::now();
        timings.result_decrypt_ms = std::chrono::duration<double, std::milli>(result_search_end - result_search_start).count();


        // ===== 统计 =====
        double total_ms = timings.query_gen_ms + timings.eval_ms + timings.result_decrypt_ms;
        sum_query_gen_ms += timings.query_gen_ms;
        sum_eval_ms      += timings.eval_ms;
        sum_decrypt_ms   += timings.result_decrypt_ms;
        sum_total_ms     += total_ms;

        std::set<int> set_time(decrypted_ids.begin(), decrypted_ids.end());


    //    5. 在客户端计算精确结果以进行验证
        std::set<int> ground_truth_ids;
        for (const auto& p : all_points_) {
            if (p.utc_timestamp >= query_box.min_ts && p.utc_timestamp < query_box.max_ts &&
                p.latitude >= query_box.min_lat && p.latitude < query_box.max_lat &&
                p.longitude >= query_box.min_lon && p.longitude < query_box.max_lon) {
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