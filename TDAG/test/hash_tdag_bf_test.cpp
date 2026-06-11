#include <gtest/gtest.h>

#include "TimeUtil.h"
#include "encrypted_db.hpp"
#include "query_plan_csv.hpp"
#include "standard_emm.hpp"
#include "cube_code.h"
#include "tdag_bf.h"
#include "adaptive_time_bucket.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct SpatiotemporalPoint {
    std::string time_str;
    long long utc_timestamp;
    double latitude;
    double longitude;
    int record_id;

    SpatiotemporalPoint(std::string time,
                        long long timestamp,
                        double lat,
                        double lon,
                        int id)
        : time_str(std::move(time)),
          utc_timestamp(timestamp),
          latitude(lat),
          longitude(lon),
          record_id(id) {}
};

struct HashTimeCandidate {
    int bucket_id;
    int left_interval;
    int right_interval;
};

void PrintQueryArgs(const std::string& start_time,
                    const std::string& end_time,
                    double lat_min, double lat_max,
                    double lon_min, double lon_max) {
    std::cout << std::fixed << std::setprecision(8);
    std::cout << "start_time = \"" << start_time << "\";\n";
    std::cout << "end_time   = \"" << end_time << "\";\n";
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

std::string GetEnvString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return value;
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

std::vector<SpatiotemporalPoint> load_spatiotemporal_data(const std::string& filepath,
                                                          std::size_t limit_n = SIZE_MAX) {
    std::vector<SpatiotemporalPoint> data;
    std::ifstream file(filepath);
    std::string line;
    std::getline(file, line);

    int record_id = 1;
    int counter = 1;
    while (std::getline(file, line)) {
        if (data.size() >= limit_n) {
            break;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::vector<std::string> record;
        size_t start = 0, end = 0;
        while ((end = line.find(",", start)) != std::string::npos) {
            record.push_back(line.substr(start, end - start));
            start = end + 1;
        }
        record.push_back(line.substr(start));

        try {
            std::string time_str = record.at(7);
            long long ts = TimeUtil::to_timestamp(time_str);
            double lat = std::stod(record.at(4));
            double lon = std::stod(record.at(5));
            data.emplace_back(time_str, ts, lat, lon, record_id++);
            ++counter;
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line " << counter + 2 << ": " << e.what() << std::endl;
        }
    }
    return data;
}

std::vector<std::string> DeduplicateCodesPreserveOrder(std::vector<std::string> codes) {
    std::vector<std::string> unique_codes;
    unique_codes.reserve(codes.size());
    std::unordered_set<std::string> seen;
    seen.reserve(codes.size() * 2 + 1);
    for (auto& code : codes) {
        if (seen.insert(code).second) {
            unique_codes.push_back(std::move(code));
        }
    }
    return unique_codes;
}

int CeilLog2Size(std::size_t value) {
    std::size_t capacity = 1;
    int height = 0;
    while (capacity < value) {
        capacity <<= 1;
        ++height;
    }
    return height;
}

std::vector<HashTimeCandidate> query_hash_tdag_bf(
    const AdaptiveTimeBucketBuilder& time_index,
    const std::vector<std::shared_ptr<TdagBF>>& bucket_tdags,
    const std::shared_ptr<CubeCode>& cube_code,
    const std::string& start_time,
    const std::string& end_time,
    double lat_min, double lat_max,
    double lon_min, double lon_max) {

    std::vector<HashTimeCandidate> result;
    const long long start_ts = TimeUtil::to_timestamp(start_time);
    const long long end_ts = TimeUtil::to_timestamp(end_time);

    std::vector<double> query_min = {lat_min, lon_min};
    std::vector<double> query_max = {lat_max, lon_max};
    auto query_codes = cube_code->generateQueryCubeCodes(query_min, query_max);

    int bucket_id = time_index.locateFirstBucketForQuery(start_ts);
    const bool debug_query = std::getenv("JXT2_DEBUG_QUERY") != nullptr;
    if (debug_query) {
        std::cout << "[QueryLocate] start_day=" << time_index.dayStart(start_ts)
                  << ", slot=" << time_index.slotInDay(start_ts)
                  << ", first_bucket=" << bucket_id << "\n";
    }

    const auto& buckets = time_index.buckets();
    while (bucket_id >= 0 && static_cast<std::size_t>(bucket_id) < buckets.size()) {
        const auto& bucket = buckets[static_cast<std::size_t>(bucket_id)];
        if (bucket.start_ts > end_ts) {
            break;
        }
        if (bucket.end_ts >= start_ts &&
            static_cast<std::size_t>(bucket_id) < bucket_tdags.size()) {
            const auto [local_l, local_r] =
                bucket.localRangeForQuery(start_ts, end_ts,
                                          time_index.config().time_slot_seconds);
            if (debug_query) {
                std::cout << "[QueryBucket] bucket_id=" << bucket.bucket_id
                          << ", local_l=" << local_l
                          << ", local_r=" << local_r << "\n";
            }
            auto tdag = bucket_tdags[static_cast<std::size_t>(bucket_id)];
            if (tdag == nullptr || local_l == -1) {
                bucket_id = bucket.next_bucket_id;
                continue;
            }
            try {
                auto cover = tdag->get_single_range_cover({local_l, local_r}, query_codes);
                if (cover.first != -1) {
                    result.push_back({bucket.bucket_id, cover.first, cover.second});
                }
            } catch (const std::exception&) {
                // No BF-matching TDAG cover for this bucket; continue scanning.
            }
        }
        bucket_id = bucket.next_bucket_id;
    }
    return result;
}

}  // namespace

class SpatiotemporalDB_HashTdag_Test : public ::testing::Test {
protected:
    void SetUp() override {
        auto start_build = std::chrono::high_resolution_clock::now();
        K_token = "89b7a92966f6eb32";
        K_enc = "7975922666f6eb02";

        std::string x = "300000";
        std::string default_path = "/home/shijw/JXT2/data/table1/table1_k7_j1_" + x + ".csv";
        std::string path_ = GetEnvString("JXT2_DATA_PATH", default_path);
        std::size_t target_N = GetEnvSizeT("JXT2_LIMIT_N", 300000);
        query_runs = GetEnvInt("JXT2_QUERY_RUNS", 100);
        all_points_ = load_spatiotemporal_data(path_, target_N);
        ASSERT_FALSE(all_points_.empty());

        std::cout << "Loaded " << all_points_.size() << " data points." << std::endl;

        const auto auto_bounds = BoundsForPoints(all_points_);
        const std::vector<double> min_bounds = {
            GetEnvDouble("JXT2_BOUND_LAT_MIN", auto_bounds.first[0]),
            GetEnvDouble("JXT2_BOUND_LON_MIN", auto_bounds.first[1])
        };
        const std::vector<double> max_bounds = {
            GetEnvDouble("JXT2_BOUND_LAT_MAX", auto_bounds.second[0]),
            GetEnvDouble("JXT2_BOUND_LON_MAX", auto_bounds.second[1])
        };
        constexpr int cube_code_level = 3;
        cube_code = std::make_shared<CubeCode>(2, min_bounds, max_bounds, cube_code_level);
        std::cout << std::fixed << std::setprecision(8)
                  << "[Bounds] lat=[" << min_bounds[0] << "," << max_bounds[0] << "], "
                  << "lon=[" << min_bounds[1] << "," << max_bounds[1] << "], "
                  << "cube_level=" << cube_code_level << std::endl;
        std::vector<AdaptiveTimePointRef> time_points;
        time_points.reserve(all_points_.size());
        for (const auto& point : all_points_) {
            time_points.push_back({point.record_id, point.utc_timestamp});
        }

        time_config = AdaptiveTimeBucketBuilder::configFromEnv();
        time_builder = AdaptiveTimeBucketBuilder(time_config);
        time_builder.build(std::move(time_points));
        time_builder.printStats(std::cout);

        bucket_tdags.clear();
        bucket_tdags.resize(time_builder.buckets().size());
        for (const auto& bucket : time_builder.buckets()) {
            const std::size_t leaf_count =
                std::max<std::size_t>(1, bucket.occupied_abs_slots.size());
            const int tdag_height = CeilLog2Size(leaf_count);
            bucket_tdags[static_cast<std::size_t>(bucket.bucket_id)] =
                TdagBF::initialize(tdag_height, 0.01, 1000, 0);
        }

        std::cout << "Building AdaptiveBucket / TdagBF / BF index..." << std::endl;

        for (const auto& point : all_points_) {
            const int bucket_id = time_builder.bucketIdForRecord(point.record_id);
            ASSERT_GE(bucket_id, 0);
            ASSERT_LT(static_cast<std::size_t>(bucket_id), time_builder.buckets().size());
            const auto& bucket = time_builder.buckets()[static_cast<std::size_t>(bucket_id)];
            const long long abs_slot = time_builder.absSlotForTimestamp(point.utc_timestamp);
            const int local_slot = bucket.localSlotForAbsSlot(abs_slot);
            ASSERT_GE(local_slot, 0);

            std::vector<double> coords = {point.latitude, point.longitude};
            auto codes = DeduplicateCodesPreserveOrder(cube_code->generateDataCubeCodes(coords));

            auto tdag = bucket_tdags[static_cast<std::size_t>(bucket_id)];
            auto ref = tdag->descend_tree(local_slot, tdag->range);
            tdag->insert_keyword_deferred(local_slot, local_slot, codes);

            for (const auto& [L, R] : ref) {
                std::string time_key = "tdag_bucket_" + std::to_string(bucket_id) + "_" +
                                       std::to_string(L) + "-" + std::to_string(R);
                keyword_map[time_key].push_back(point.record_id);
            }
            for (const auto& code : codes) {
                keyword_map[code].push_back(point.record_id);
            }
        }

        std::cout << "[AdaptiveBucket] Active Buckets before finalize: "
                  << time_builder.buckets().size() << std::endl;
        auto finalize_start = std::chrono::high_resolution_clock::now();
        std::cout << "[TdagBF] Finalizing deferred Bloom Filters..." << std::endl;
        for (auto& tdag : bucket_tdags) {
            if (tdag) {
                tdag->finalize_bloom_filters();
            }
        }
        auto finalize_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> finalize_time = finalize_end - finalize_start;
        std::cout << "[TdagBF] Finalize complete: " << finalize_time.count() << " ms" << std::endl;

        std::cout << "Index build complete." << std::endl;
        std::cout << "[AdaptiveBucket] Active Buckets: "
                  << time_builder.buckets().size() << std::endl;

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

        start_time = GetEnvString("JXT2_QUERY_START", fallback_day + " 00:00:00+00");
        end_time = GetEnvString("JXT2_QUERY_END", fallback_day + " 23:59:59+00");
        lat_min = GetEnvDouble("JXT2_QUERY_LAT_MIN", fallback_lat_min);
        lat_max = GetEnvDouble("JXT2_QUERY_LAT_MAX", fallback_lat_max);
        lon_min = GetEnvDouble("JXT2_QUERY_LON_MIN", fallback_lon_min);
        lon_max = GetEnvDouble("JXT2_QUERY_LON_MAX", fallback_lon_max);
        PrintQueryArgs(start_time, end_time, lat_min, lat_max, lon_min, lon_max);

        emm_engine = std::make_unique<StandardEMM>(K_token, K_enc);
        emm_engine->buildEMM(keyword_map);
        std::cout << "Encrypted index build complete." << std::endl;

        auto end_build = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> build_time = end_build - start_build;
        std::cout << "Build time: " << build_time.count() << " ms\n";

        size_t storage_size_bytes = emm_engine->getStorageSize();
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[Storage] Total Index Size: " << storage_size_bytes / 1024.0
                  << " KB" << std::endl;
        std::cout << "[Storage] Adaptive Bucket Metadata: "
                  << time_builder.metadataSizeBytes() / 1024.0 << " KB" << std::endl;
    }

    AdaptiveTimeBucketConfig time_config;
    AdaptiveTimeBucketBuilder time_builder;
    std::vector<std::shared_ptr<TdagBF>> bucket_tdags;
    std::shared_ptr<CubeCode> cube_code;
    std::vector<SpatiotemporalPoint> all_points_;
    std::unordered_map<std::string, std::vector<int>> keyword_map;
    double lat_min, lat_max, lon_min, lon_max;
    std::string start_time, end_time;
    std::string K_token, K_enc;
    int query_runs = 100;
    std::unique_ptr<StandardEMM> emm_engine;
};

TEST_F(SpatiotemporalDB_HashTdag_Test, PerformSpatiotemporalQueryAndVerify) {
    std::cout << "\n--- Hash TDAG/BF Spatiotemporal Query Test(" << query_runs
              << " runs) ---" << std::endl;

    const std::string query_plan_path = QueryPlanPathFromEnv("JXT2_QUERY_PLAN");
    if (!query_plan_path.empty()) {
        const std::string dataset_filter = GetEnvString("JXT2_DATASET", "");
        const auto rows = LoadQueryPlanRows(query_plan_path, dataset_filter);
        std::cout << "[QPLAN] loaded_rows=" << rows.size()
                  << " path=" << query_plan_path << "\n";
        for (const auto& row : rows) {
            QueryTimings timings;
            const long long start_ts = TimeUtil::to_timestamp(row.query_start);
            const long long end_ts = TimeUtil::to_timestamp(row.query_end);

            const auto query_search_start = std::chrono::high_resolution_clock::now();
            auto time_cands = query_hash_tdag_bf(time_builder, bucket_tdags, cube_code,
                                                 row.query_start, row.query_end,
                                                 row.lat_min, row.lat_max,
                                                 row.lon_min, row.lon_max);
            const auto query_search_end = std::chrono::high_resolution_clock::now();
            timings.query_gen_ms =
                std::chrono::duration<double, std::milli>(
                    query_search_end - query_search_start).count();

            std::vector<std::string> time_tokens;
            time_tokens.reserve(time_cands.size());
            for (const auto& cand : time_cands) {
                std::string time_key = "tdag_bucket_" + std::to_string(cand.bucket_id) + "_" +
                                       std::to_string(cand.left_interval) + "-" +
                                       std::to_string(cand.right_interval);
                time_tokens.push_back(time_key);
            }
            auto time_base_tokens = emm_engine->generateTokens(time_tokens);

            const auto eval_search_start = std::chrono::high_resolution_clock::now();
            EncryptedResult enc_time = emm_engine->query(time_base_tokens);
            const auto eval_search_end = std::chrono::high_resolution_clock::now();
            timings.eval_ms =
                std::chrono::duration<double, std::milli>(
                    eval_search_end - eval_search_start).count();

            const auto result_search_start = std::chrono::high_resolution_clock::now();
            auto dec_time = emm_engine->decryptResults(enc_time);
            const auto result_search_end = std::chrono::high_resolution_clock::now();
            timings.result_decrypt_ms =
                std::chrono::duration<double, std::milli>(
                    result_search_end - result_search_start).count();

            const double total_ms =
                timings.query_gen_ms + timings.eval_ms + timings.result_decrypt_ms;

            std::set<int> candidate_ids(dec_time.begin(), dec_time.end());
            std::set<int> ground_truth_ids;
            for (const auto& point : all_points_) {
                if (point.utc_timestamp >= start_ts && point.utc_timestamp <= end_ts &&
                    point.latitude >= row.lat_min && point.latitude <= row.lat_max &&
                    point.longitude >= row.lon_min && point.longitude <= row.lon_max) {
                    ground_truth_ids.insert(point.record_id);
                }
            }

            EXPECT_GE(candidate_ids.size(), ground_truth_ids.size());
            EXPECT_TRUE(std::includes(candidate_ids.begin(), candidate_ids.end(),
                                      ground_truth_ids.begin(), ground_truth_ids.end()))
                << "Hash TDAG/BF query-plan result does not fully include ground truth";

            std::size_t false_positive_count = 0;
            for (const auto id : candidate_ids) {
                if (ground_truth_ids.find(id) == ground_truth_ids.end()) {
                    ++false_positive_count;
                }
            }

            PrintQueryPlanResult("DSTF+", row,
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

    int print_every = std::max(1, query_runs / 10);
    for (int run = 1; run <= query_runs; ++run) {
        QueryTimings timings;

        auto query_search_start = std::chrono::high_resolution_clock::now();
        long long start_ts = TimeUtil::to_timestamp(start_time);
        long long end_ts = TimeUtil::to_timestamp(end_time);
        auto time_cands = query_hash_tdag_bf(time_builder, bucket_tdags, cube_code,
                                             start_time, end_time,
                                             lat_min, lat_max, lon_min, lon_max);
        auto query_search_end = std::chrono::high_resolution_clock::now();
        timings.query_gen_ms =
            std::chrono::duration<double, std::milli>(query_search_end - query_search_start).count();

        std::vector<std::string> time_tokens;
        time_tokens.reserve(time_cands.size());
        for (const auto& cand : time_cands) {
            std::string time_key = "tdag_bucket_" + std::to_string(cand.bucket_id) + "_" +
                                   std::to_string(cand.left_interval) + "-" +
                                   std::to_string(cand.right_interval);
            time_tokens.push_back(time_key);
        }
        auto time_base_tokens = emm_engine->generateTokens(time_tokens);

        auto eval_search_start = std::chrono::high_resolution_clock::now();
        EncryptedResult enc_time = emm_engine->query(time_base_tokens);
        auto eval_search_end = std::chrono::high_resolution_clock::now();
        timings.eval_ms =
            std::chrono::duration<double, std::milli>(eval_search_end - eval_search_start).count();

        auto result_search_start = std::chrono::high_resolution_clock::now();
        auto dec_time = emm_engine->decryptResults(enc_time);
        auto result_search_end = std::chrono::high_resolution_clock::now();
        timings.result_decrypt_ms =
            std::chrono::duration<double, std::milli>(result_search_end - result_search_start).count();

        double total_ms = timings.query_gen_ms + timings.eval_ms + timings.result_decrypt_ms;
        sum_query_gen_ms += timings.query_gen_ms;
        sum_eval_ms += timings.eval_ms;
        sum_decrypt_ms += timings.result_decrypt_ms;
        sum_total_ms += total_ms;

        std::set<int> ids_time(dec_time.begin(), dec_time.end());
        std::set<int> ground_truth_ids;
        for (const auto& point : all_points_) {
            if (point.utc_timestamp >= start_ts && point.utc_timestamp <= end_ts &&
                point.latitude >= lat_min && point.latitude <= lat_max &&
                point.longitude >= lon_min && point.longitude <= lon_max) {
                ground_truth_ids.insert(point.record_id);
            }
        }

        EXPECT_GE(ids_time.size(), ground_truth_ids.size());
        EXPECT_TRUE(std::includes(ids_time.begin(), ids_time.end(),
                                  ground_truth_ids.begin(), ground_truth_ids.end()))
            << "Hash TDAG/BF result does not fully include ground truth";

        last_ground_truth = ground_truth_ids.size();
        last_returned = ids_time.size();

        if (run % print_every == 0) {
            std::cout << "[Run " << run << "] latency(ms): gen=" << timings.query_gen_ms
                      << ", eval=" << timings.eval_ms
                      << ", dec=" << timings.result_decrypt_ms
                      << ", total=" << total_ms
                      << " | returned=" << last_returned
                      << ", truth=" << last_ground_truth << "\n";
        }
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "========================================================\n";
    std::cout << "Query Time (Client Token Gen): " << (sum_query_gen_ms / query_runs) << " ms\n";
    std::cout << "Eval Time (Server Evaluation): " << (sum_eval_ms / query_runs) << " ms\n";
    std::cout << "Result Time (Client Decrypt):  " << (sum_decrypt_ms / query_runs) << " ms\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Total Query Latency:           " << (sum_total_ms / query_runs) << " ms\n";
    std::cout << "Last Returned / Truth    " << last_returned << " / " << last_ground_truth << "\n";
    std::cout << "========================================================\n";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
