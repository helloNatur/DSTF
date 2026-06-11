#include <gtest/gtest.h>

#include "TimeUtil.h"
#include "bplus_tdag.h"
#include "i1_common_test_utils.hpp"
#include "tdag_bf.h"

#include <map>
#include <memory>

class I1BPTdagBFTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto start_build = std::chrono::high_resolution_clock::now();

        std::vector<double> min_bounds = {35.51018469, 139.4708776};
        std::vector<double> max_bounds = {35.86715042, 139.9125932};
        day_locator = std::make_shared<BPlusTdag>(2, min_bounds, max_bounds, 3);

        const std::string path = "/home/shijw/JXT2/data/table1/table1_k7_j1_300000.csv";
        const std::size_t target_n = I1GetEnvSizeT("JXT2_LIMIT_N", 300000);
        query_runs = I1GetEnvInt("JXT2_QUERY_RUNS", 100);
        all_points = I1LoadSpatiotemporalData(path, target_n);
        ASSERT_FALSE(all_points.empty());

        min_day_ts = TimeUtil::date_to_timestamp(all_points.front().time_str.substr(0, 10));
        for (const auto& point : all_points) {
            long long day_ts = TimeUtil::date_to_timestamp(point.time_str.substr(0, 10));
            min_day_ts = std::min(min_day_ts, day_ts);
        }

        std::map<long long, std::vector<LogarithmicSrcI1Index::Entry>> entries_by_day;
        for (const auto& point : all_points) {
            long long day_ts = TimeUtil::date_to_timestamp(point.time_str.substr(0, 10));
            int interval = TimeUtil::time_to_10min_interval(point.time_str);
            entries_by_day[day_ts].push_back({interval, point.record_id});
        }

        KeywordMap keyword_map;
        for (const auto& [day_ts, entries] : entries_by_day) {
            auto index = std::make_unique<LogarithmicSrcI1Index>();
            index->build(entries, keyword_map, "b_i1_day_" + std::to_string(day_ts));
            auto dummy_tdag = TdagBF::initialize(0, 0.01, 1, 0);
            day_locator->insert(day_ts, dummy_tdag);
            day_indexes.emplace(day_ts, std::move(index));
        }

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
        std::cout << "[I1] Day Buckets: " << day_indexes.size() << std::endl;
        std::cout << "Build time: " << build_time.count() << " ms\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[Storage] Total Index Size: "
                  << emm_engine->getStorageSize() / 1024.0 << " KB" << std::endl;
    }

    std::vector<int> queryDay(long long day_ts, int start_interval, int end_interval,
                              I1QueryTimings& timings) {
        auto it = day_indexes.find(day_ts);
        if (it == day_indexes.end()) {
            return {};
        }
        return I1RunQuery(*it->second, *emm_engine, start_interval, end_interval, timings);
    }

    std::shared_ptr<BPlusTdag> day_locator;
    std::map<long long, std::unique_ptr<LogarithmicSrcI1Index>> day_indexes;
    std::vector<I1SpatiotemporalPoint> all_points;
    std::unique_ptr<StandardEMM> emm_engine;
    long long min_day_ts = 0;
    int query_runs = 100;
    std::string start_time;
    std::string end_time;
    double lat_min = 0.0;
    double lat_max = 0.0;
    double lon_min = 0.0;
    double lon_max = 0.0;
};

TEST_F(I1BPTdagBFTest, PerformSpatiotemporalQueryAndVerify) {
    std::cout << "\n--- B+ Logarithmic-SRC-i1 TDAG/BF Query Test("
              << query_runs << " runs) ---" << std::endl;

    double sum_query_gen_ms = 0.0;
    double sum_eval_ms = 0.0;
    double sum_decrypt_ms = 0.0;
    double sum_total_ms = 0.0;
    std::size_t last_ground_truth = 0;
    std::size_t last_returned = 0;

    const long long start_ts = TimeUtil::to_timestamp(start_time);
    const long long end_ts = TimeUtil::to_timestamp(end_time);
    const long long start_day = TimeUtil::date_to_timestamp(start_time.substr(0, 10));
    const long long end_day = TimeUtil::date_to_timestamp(end_time.substr(0, 10));
    const int start_interval = TimeUtil::time_to_10min_interval(start_time);
    const int end_interval = TimeUtil::time_to_10min_interval(end_time);
    const int print_every = std::max(1, query_runs / 10);

    for (int run = 1; run <= query_runs; ++run) {
        I1QueryTimings timings;
        std::vector<int> result_ids;

        auto query_gen_start = std::chrono::high_resolution_clock::now();
        auto days = day_locator->rangeSearch(start_day, end_day);
        auto query_gen_end = std::chrono::high_resolution_clock::now();
        timings.query_gen_ms +=
            std::chrono::duration<double, std::milli>(query_gen_end - query_gen_start).count();

        for (const auto& [day_ts, ignored] : days) {
            int sh = (day_ts == start_day) ? start_interval : 0;
            int eh = (day_ts == end_day) ? end_interval : 143;
            auto day_ids = queryDay(day_ts, sh, eh, timings);
            result_ids.insert(result_ids.end(), day_ids.begin(), day_ids.end());
        }

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
