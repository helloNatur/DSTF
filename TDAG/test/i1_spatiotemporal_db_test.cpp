#include <gtest/gtest.h>

#include "TimeUtil.h"
#include "i1_common_test_utils.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace {

int ToTimeGrid(long long ts, long long min_ts, long long max_ts, int grid_max) {
    if (max_ts <= min_ts) {
        return 0;
    }
    double normalized = static_cast<double>(ts - min_ts) /
                        static_cast<double>(max_ts - min_ts);
    int grid = static_cast<int>(normalized * grid_max);
    if (grid < 0) {
        return 0;
    }
    if (grid > grid_max) {
        return grid_max;
    }
    return grid;
}

}  // namespace

TEST(LogarithmicSrcI1IndexUnitTest, I2QueryLabelsAreUnique) {
    KeywordMap keyword_map;
    LogarithmicSrcI1Index index;
    index.build({{0, 101}, {0, 102}, {1, 201}, {1, 202}},
                keyword_map,
                "unit_i1");

    const std::vector<std::pair<int, int>> position_ranges = {
        {0, 1}, {0, 1}, {0, 2}, {0, 2}
    };
    const auto labels = index.getI2QueryLabels(position_ranges);
    const std::set<Label> unique_labels(labels.begin(), labels.end());

    EXPECT_EQ(unique_labels.size(), labels.size());
    EXPECT_LT(unique_labels.size(), position_ranges.size());
}

class I1SpatiotemporalDBTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto start_build = std::chrono::high_resolution_clock::now();

        const std::string path = "/home/shijw/JXT2/data/table1/table1_k7_j1_300000.csv";
        const std::size_t target_n = I1GetEnvSizeT("JXT2_LIMIT_N", 300000);
        query_runs = I1GetEnvInt("JXT2_QUERY_RUNS", 100);
        all_points = I1LoadSpatiotemporalData(path, target_n);
        ASSERT_FALSE(all_points.empty());

        auto [min_it, max_it] = std::minmax_element(
            all_points.begin(), all_points.end(),
            [](const auto& a, const auto& b) { return a.utc_timestamp < b.utc_timestamp; });
        min_ts = min_it->utc_timestamp;
        max_ts = max_it->utc_timestamp;

        std::vector<LogarithmicSrcI1Index::Entry> entries;
        entries.reserve(all_points.size());
        for (const auto& point : all_points) {
            entries.push_back({
                ToTimeGrid(point.utc_timestamp, min_ts, max_ts, grid_max),
                point.record_id
            });
        }

        KeywordMap keyword_map;
        i1_index.build(entries, keyword_map, "spatiotemporal_i1_time");

        emm_engine = std::make_unique<StandardEMM>("89b7a92966f6eb32", "7975922666f6eb02");
        emm_engine->buildEMM(keyword_map);

        start_time = "2012-04-05 01:20:00+00";
        end_time = "2012-04-05 22:40:00+00";
        lat_min = 35.6798362;
        lat_max = 35.69325604;
        lon_min = 139.6678108;
        lon_max = 139.7722312;

        auto end_build = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> build_time = end_build - start_build;

        std::cout << "Loaded " << all_points.size() << " data points." << std::endl;
        std::cout << "[I1] Unique time-grid values: " << i1_index.uniqueValueCount() << std::endl;
        std::cout << "Build time: " << build_time.count() << " ms\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[Storage] Total Index Size: "
                  << emm_engine->getStorageSize() / 1024.0 << " KB" << std::endl;
    }

    std::vector<I1SpatiotemporalPoint> all_points;
    LogarithmicSrcI1Index i1_index;
    std::unique_ptr<StandardEMM> emm_engine;
    long long min_ts = 0;
    long long max_ts = 0;
    static constexpr int grid_max = 127;
    int query_runs = 100;
    std::string start_time;
    std::string end_time;
    double lat_min = 0.0;
    double lat_max = 0.0;
    double lon_min = 0.0;
    double lon_max = 0.0;
};

TEST_F(I1SpatiotemporalDBTest, PerformanceBenchmark) {
    std::cout << "\n--- Logarithmic-SRC-i1 Spatiotemporal Query Test("
              << query_runs << " runs) ---\n" << std::endl;

    double sum_query_gen_ms = 0.0;
    double sum_eval_ms = 0.0;
    double sum_decrypt_ms = 0.0;
    double sum_total_ms = 0.0;
    std::size_t last_ground_truth = 0;
    std::size_t last_returned = 0;

    const long long start_ts = TimeUtil::to_timestamp(start_time);
    const long long end_ts = TimeUtil::to_timestamp(end_time);
    const int attr_start = ToTimeGrid(start_ts, min_ts, max_ts, grid_max);
    const int attr_end = ToTimeGrid(end_ts, min_ts, max_ts, grid_max);
    const int print_every = std::max(1, query_runs / 10);

    for (int run = 1; run <= query_runs; ++run) {
        I1QueryTimings timings;
        auto result_ids = I1RunQuery(i1_index, *emm_engine, attr_start, attr_end, timings);
        double total_ms = timings.query_gen_ms + timings.eval_ms + timings.result_decrypt_ms;

        sum_query_gen_ms += timings.query_gen_ms;
        sum_eval_ms += timings.eval_ms;
        sum_decrypt_ms += timings.result_decrypt_ms;
        sum_total_ms += total_ms;

        std::set<int> result_set(result_ids.begin(), result_ids.end());
        auto ground_truth = I1GroundTruth(all_points, start_ts, end_ts,
                                          lat_min, lat_max, lon_min, lon_max);

        EXPECT_GE(result_set.size(), ground_truth.size());
        EXPECT_TRUE(std::includes(result_set.begin(), result_set.end(),
                                  ground_truth.begin(), ground_truth.end()));

        last_ground_truth = ground_truth.size();
        last_returned = result_set.size();

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
