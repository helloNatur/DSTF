#include <gtest/gtest.h>
#include "encrypted_db.hpp"
#include "qdag_src_3d.hpp" // 引入新方案
#include "standard_emm.hpp"
#include "coordinate_transformer.hpp"
#include "query_plan_csv.hpp"
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <unordered_set>
#include <set>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <limits>


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

double GetEnvDouble(const char* name, double fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        return fallback;
    }
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

std::string GetEnvString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return value;
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
            time_str = record[7];
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

std::pair<std::vector<double>, std::vector<double>> BoundsForPoints(
    const std::vector<SpatiotemporalPoint>& points) {
    double min_lat = std::numeric_limits<double>::max();
    double min_lon = std::numeric_limits<double>::max();
    double max_lat = std::numeric_limits<double>::lowest();
    double max_lon = std::numeric_limits<double>::lowest();
    for (const auto& point : points) {
        min_lat = std::min(min_lat, point.latitude);
        min_lon = std::min(min_lon, point.longitude);
        max_lat = std::max(max_lat, point.latitude);
        max_lon = std::max(max_lon, point.longitude);
    }
    const double lat_pad = std::max(1e-6, (max_lat - min_lat) * 0.001);
    const double lon_pad = std::max(1e-6, (max_lon - min_lon) * 0.001);
    return {{min_lat - lat_pad, min_lon - lon_pad},
            {max_lat + lat_pad, max_lon + lon_pad}};
}





TEST(QuadTree3DSRCUnitTest, ContainingRangeCoversAreUniqueForGridPoint) {
    QuadTree3DSRC tree(4, true);
    const GridPoint3D point(8, 8, 8);

    const auto covers = tree.findContainingRangeCovers(point);
    std::unordered_set<Rect3D> unique_covers;
    for (const auto& cover : covers) {
        unique_covers.insert(cover);
    }

    EXPECT_EQ(unique_covers.size(), covers.size())
        << "QDAG cover traversal returned duplicate ranges for one grid point";
}

TEST(QuadTree3DSRCUnitTest, UnitRangeCoverDoesNotFallBackToRoot) {
    QuadTree3DSRC tree(4, true);
    const Rect3D query(GridPoint3D(5, 9, 13), GridPoint3D(6, 10, 14));

    const Rect3D cover = tree.getSingleRangeCover(query);

    EXPECT_EQ(cover.start, query.start);
    EXPECT_EQ(cover.end, query.end);
    EXPECT_FALSE(cover == tree.getRootRect())
        << "Unit-size SRC queries must use the leaf cube instead of the root cover";
}

TEST(CoordinateTransformerUnitTest, QueryUpperBoundIncludesOverlappingGridCells) {
    CoordinateTransformer transformer(0LL, 100LL, 4);
    SpatiotemporalQueryBox query_box{
        .min_ts = 20LL,
        .max_ts = 21LL,
        .min_lat = 30.0,
        .max_lat = 35.0,
        .min_lon = -100.0,
        .max_lon = -90.0
    };
    const Rect3D grid_rect = transformer.to_grid_rect(query_box);
    const SpatiotemporalPoint point(
        "synthetic",
        20LL,
        33.0,
        -95.0,
        1);
    const GridPoint3D grid_point = transformer.to_grid_point(point);

    EXPECT_TRUE(grid_rect.containsPoint(grid_point))
        << "A point inside the continuous query must be included by the grid query";

    QuadTree3DSRC tree(4, true);
    const Rect3D cover = tree.getSingleRangeCover(grid_rect);
    const auto point_covers = tree.findContainingRangeCovers(grid_point);
    EXPECT_TRUE(cover.containsPoint(grid_point));
    EXPECT_NE(std::find(point_covers.begin(), point_covers.end(), cover), point_covers.end())
        << "The query cover must be one of the labels assigned to this point";
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
        std::string default_path = "/home/shijw/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        std::string path_ = GetEnvString("JXT2_DATA_PATH", default_path);
        std::size_t target_N = GetEnvSizeT("JXT2_LIMIT_N", 300000);
        all_points_ = load_spatiotemporal_data(path_,target_N);
        ASSERT_FALSE(all_points_.empty());

        std::cout << "Loaded " << all_points_.size() << " data points." << std::endl;

        // 2. 分析数据范围以初始化转换器
        auto [min_it, max_it] = std::minmax_element(all_points_.begin(), all_points_.end(), 
            [](const auto& a, const auto& b){ return a.utc_timestamp < b.utc_timestamp; });
        long long min_ts = min_it->utc_timestamp;
        long long max_ts = max_it->utc_timestamp;
        const auto auto_bounds = BoundsForPoints(all_points_);
        const auto& center_point = all_points_[all_points_.size() / 2];
        const std::string fallback_day = center_point.time_str.substr(0, 10);
        const double lat_span = auto_bounds.second[0] - auto_bounds.first[0];
        const double lon_span = auto_bounds.second[1] - auto_bounds.first[1];
        const double lat_delta = std::max(1e-6, lat_span * 0.02);
        const double lon_delta = std::max(1e-6, lon_span * 0.02);
        const double fallback_lat_min =
            std::max(auto_bounds.first[0], center_point.latitude - lat_delta);
        const double fallback_lat_max =
            std::min(auto_bounds.second[0], center_point.latitude + lat_delta);
        const double fallback_lon_min =
            std::max(auto_bounds.first[1], center_point.longitude - lon_delta);
        const double fallback_lon_max =
            std::min(auto_bounds.second[1], center_point.longitude + lon_delta);

        
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
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "Build time: " << build_time.count() << " ms\n";
        //  --- 评测存储空间 (Storage Overhead) ---
        size_t storage_size_bytes = db_->getStorageSize();

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[Storage] Total Index Size: " << storage_size_bytes / 1024.0 << " KB" << std::endl;

        query_runs = GetEnvInt("JXT2_QUERY_RUNS", 100);
        query_box = {
            .min_ts = TimeUtil::to_timestamp(
                GetEnvString("JXT2_QUERY_START", fallback_day + " 00:00:00+00")),
            .max_ts = TimeUtil::to_timestamp(
                GetEnvString("JXT2_QUERY_END", fallback_day + " 23:59:59+00")),
            .min_lat = GetEnvDouble("JXT2_QUERY_LAT_MIN", fallback_lat_min),
            .max_lat = GetEnvDouble("JXT2_QUERY_LAT_MAX", fallback_lat_max),
            .min_lon = GetEnvDouble("JXT2_QUERY_LON_MIN", fallback_lon_min),
            .max_lon = GetEnvDouble("JXT2_QUERY_LON_MAX", fallback_lon_max)
















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
    int query_runs = 100;
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
    std::cout << "\n--- Spatiotemporal Query Test(" << query_runs << " runs) ---\n" << std::endl;

    const std::string query_plan_path = QueryPlanPathFromEnv("JXT2_QUERY_PLAN");
    if (!query_plan_path.empty()) {
        const std::string dataset_filter = GetEnvString("JXT2_DATASET", "");
        const auto rows = LoadQueryPlanRows(query_plan_path, dataset_filter);
        std::cout << "[QPLAN] loaded_rows=" << rows.size()
                  << " path=" << query_plan_path << "\n";
        for (const auto& row : rows) {
            SpatiotemporalQueryBox local_query_box{
                .min_ts = TimeUtil::to_timestamp(row.query_start),
                .max_ts = TimeUtil::to_timestamp(row.query_end),
                .min_lat = row.lat_min,
                .max_lat = row.lat_max,
                .min_lon = row.lon_min,
                .max_lon = row.lon_max
            };

            QueryTimings timings;
            const auto query_search_start = std::chrono::high_resolution_clock::now();
            const Rect3D grid_rect = transformer_->to_grid_rect(local_query_box);
            auto query_labels = qdag_scheme_->getQueryLabels(grid_rect);
            auto search_tokens = emm_engine_->generateTokens(query_labels);
            const auto query_search_end = std::chrono::high_resolution_clock::now();
            timings.query_gen_ms =
                std::chrono::duration<double, std::milli>(
                    query_search_end - query_search_start).count();

            const auto eval_search_start = std::chrono::high_resolution_clock::now();
            auto encrypted_results = emm_engine_->query(search_tokens);
            const auto eval_search_end = std::chrono::high_resolution_clock::now();
            timings.eval_ms =
                std::chrono::duration<double, std::milli>(
                    eval_search_end - eval_search_start).count();

            const auto result_search_start = std::chrono::high_resolution_clock::now();
            auto decrypted_ids = emm_engine_->decryptResults(encrypted_results);
            const auto result_search_end = std::chrono::high_resolution_clock::now();
            timings.result_decrypt_ms =
                std::chrono::duration<double, std::milli>(
                    result_search_end - result_search_start).count();

            const double total_ms =
                timings.query_gen_ms + timings.eval_ms + timings.result_decrypt_ms;

            std::set<int> candidate_ids(decrypted_ids.begin(), decrypted_ids.end());
            std::set<int> ground_truth_ids;
            for (const auto& p : all_points_) {
                if (p.utc_timestamp >= local_query_box.min_ts &&
                    p.utc_timestamp < local_query_box.max_ts &&
                    p.latitude >= local_query_box.min_lat &&
                    p.latitude < local_query_box.max_lat &&
                    p.longitude >= local_query_box.min_lon &&
                    p.longitude < local_query_box.max_lon) {
                    ground_truth_ids.insert(p.record_id);
                }
            }

            EXPECT_GE(candidate_ids.size(), ground_truth_ids.size());
            EXPECT_TRUE(std::includes(candidate_ids.begin(), candidate_ids.end(),
                                      ground_truth_ids.begin(), ground_truth_ids.end()))
                << "Qdag-SRC query-plan result does not fully include ground truth";

            std::size_t false_positive_count = 0;
            for (const auto id : candidate_ids) {
                if (ground_truth_ids.find(id) == ground_truth_ids.end()) {
                    ++false_positive_count;
                }
            }

            PrintQueryPlanResult("Qdag-SRC", row,
                                 timings.query_gen_ms,
                                 timings.eval_ms,
                                 timings.result_decrypt_ms,
                                 total_ms,
                                 candidate_ids.size(),
                                 ground_truth_ids.size(),
                                 false_positive_count);
        }
        return;
    }

    double sum_query_gen_ms = 0, sum_eval_ms = 0, sum_decrypt_ms = 0, sum_total_ms = 0;
    std::size_t last_ground_truth = 0, last_returned = 0;
    const int print_every = std::max(1, query_runs / 10);

    for (int run = 1; run <= query_runs; ++run)
    {
        QueryTimings timings;
        // 2.1. Query Time: 生成查询令牌
        auto query_search_start = std::chrono::high_resolution_clock::now();

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
    std::cout << "Result Time (Client Decrypt):  " << (sum_decrypt_ms   / query_runs) << " ms\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Total Query Latency:           " << (sum_total_ms     / query_runs) << " ms\n";
    std::cout << "Last Returned / Truth    " << last_returned << " / " << last_ground_truth << "\n";
    std::cout << "========================================================\n";
}
