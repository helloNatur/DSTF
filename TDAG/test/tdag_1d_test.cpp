#include <gtest/gtest.h>
#include "tdag.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <random>

// ===================================================================================
// =================== Tdag Test Suite ==============================================
// ===================================================================================

class TdagTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize Tdag with a fixed height for testing
        // Height=4: domain [0, 15], small for unit tests
        height_ = 4;
        root_ = Tdag::initialize(height_);
        ASSERT_TRUE(root_ != nullptr);
        max_val_ = (1 << height_) - 1;  // 15 for h=4

        // For performance, use larger height=10: domain [0, 1023]
        perf_height_ = 10;
        perf_root_ = Tdag::initialize(perf_height_);
        ASSERT_TRUE(perf_root_ != nullptr);

        // Synthetic data for benchmark: 1000 random points in [0, 1023]
        std::random_device rd; //随机种子
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, (1 << perf_height_) - 1);
        perf_points_.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            perf_points_.emplace_back(dis(gen));//在生成 1000 个均匀分布的随机整数，范围是 [0, 1023]，放进 perf_points_ 里。
        }
    }

    int height_;
    std::shared_ptr<Tdag> root_;
    int max_val_;

    // Performance setup
    int perf_height_;
    std::shared_ptr<Tdag> perf_root_;
    std::vector<int> perf_points_;

    // 判断点val是否在区间rng内
    bool range_contains(const std::pair<int, int>& rng, int val) const {
        return rng.first <= val && rng.second >= val;
    }

    // Helper to compute expected path for descend_tree (manual simulation)
    std::vector<std::pair<int, int>> expected_path(int val) const {
        std::vector<std::pair<int, int>> path;
        std::pair<int, int> current = {0, max_val_};
        while (current.first != val || current.second != val) {
            path.push_back(current);
            int mid = current.first + (current.second - current.first) / 2; //15/2=7
            int qtr = (current.second - current.first) / 4; //15/4=3
            int mid0 = mid - qtr;
            int mid1 = mid + qtr + ((current.second - current.first) % 4 > 0 ? 1 : 0);
            if (val >= mid0 && val <= mid1 && (current.second - current.first) > 1) {
                path.push_back({mid0, mid1});
            }
            if (val <= mid) {
                current.second = mid;
            } else {
                current.first = mid + 1;
            }
        }
        path.push_back({val, val});
        // Remove duplicates
        std::sort(path.begin(), path.end());
        auto it = std::unique(path.begin(), path.end());
        path.resize(it - path.begin());
        return path;
    }
};

// ===================================================================================
// =================== 1. 功能正确性测试 (Functional Correctness) ==================
// ===================================================================================

// Test descend_tree: Ensures the path from root to leaf includes all containing ranges
TEST_F(TdagTest, DescendTreeCorrectness) {
    // Test edge cases
    EXPECT_EQ(root_->descend_tree(0, {0, max_val_}).size(), 5);  // Full path for 0
    EXPECT_EQ(root_->descend_tree(max_val_, {0, max_val_}).size(), 5);  // Full path for 15

    // Test middle points
    int mid_val = max_val_ / 2;  // 7
    auto path = root_->descend_tree(mid_val, {0, max_val_});
    EXPECT_EQ(path.size(), 6);  // Should include middle ranges
    //在0-15之间不应该返回8个节点{[0,15],[4,11],[0,7],[4,7],[6,9],[6,7],[7,8],[7,7]},
    // 而是{[0,15],[4,11],[0,7],[4,7],[6,7],[7,7]}，因为再进 [6,7]，长度 1 → 不再生成桥接段
    //闭区间、每层只生成一个中间段，且 |R-L|>1 才生成
    // 归一化（排序去重），避免顺序影响
    auto norm = [](auto& v){ std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    norm(path);

    std::vector<std::pair<int,int>> expected = {{0,15},{4,11},{0,7},{4,7},{6,7},{7,7}};
    norm(expected);

    EXPECT_EQ(path, expected);
    for (const auto& rng : path) {
        EXPECT_TRUE(range_contains(rng, mid_val));
    }

    // Verify against expected
    for (int val = 0; val <= max_val_; ++val) {
        auto actual = root_->descend_tree(val, {0, max_val_});
        // 去重 + 排序（按 [L,R] 词典序）
        auto norm = [](std::vector<std::pair<int,int>>& v){
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        norm(actual);

        auto expected = expected_path(val);
        EXPECT_EQ(actual, expected) << "Mismatch for val=" << val;
    }
}

// Test get_single_range_cover: Ensures it returns a single minimal containing range
TEST_F(TdagTest, GetSingleRangeCoverCorrectness) {
    // Test full domain
    auto full_cover = root_->get_single_range_cover({0, max_val_});
    EXPECT_EQ(full_cover, std::make_pair(0, max_val_));

    // Test single point
    for (int val = 0; val <= max_val_; ++val) {
        auto cover = root_->get_single_range_cover({val, val});
        EXPECT_TRUE(range_contains(cover, val));
        // Should be minimal: for points, often a small middle or leaf
        EXPECT_LE(cover.second - cover.first, 3);  // Rough check for h=4
    }

    // Test small ranges
    auto small_cover = root_->get_single_range_cover({3, 5});//[0,7]
    EXPECT_TRUE(range_contains(small_cover, 3) && range_contains(small_cover, 5));
    EXPECT_LE(small_cover.second - small_cover.first, 7);  // Within half

    // Test large but not full
    auto large_cover = root_->get_single_range_cover({1, 14});
    EXPECT_EQ(large_cover, std::make_pair(0, max_val_));  // Should fallback to root-ish
}

// Test interval_contains_interval utility
TEST_F(TdagTest, IntervalContains) {
    EXPECT_TRUE(root_->interval_contains_interval({0, 15}, {0, 15}));
    EXPECT_TRUE(root_->interval_contains_interval({0, 15}, {5, 10}));
    EXPECT_FALSE(root_->interval_contains_interval({5, 10}, {0, 15}));
    EXPECT_FALSE(root_->interval_contains_interval({0, 5}, {6, 10}));
}

// ===================================================================================
// =================== 2. 性能基准测试 (Performance Benchmark) ======================
// ===================================================================================

// Benchmark: Time descend_tree for many points
TEST_F(TdagTest, PerformanceDescendTree) {
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::vector<std::pair<int, int>>> paths;
    paths.reserve(perf_points_.size());
    for (int val : perf_points_) {
        paths.push_back(perf_root_->descend_tree(val, {0, (1 << perf_height_) - 1}));
    }
    auto end = std::chrono::high_resolution_clock::now();
    double avg_time_ms = std::chrono::duration<double, std::milli>(end - start).count() / perf_points_.size();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Average descend_tree time per point: " << avg_time_ms << " ms" << std::endl;
    std::cout << "Total paths generated: " << paths.size() << std::endl;
    std::cout << "Average path length: ";
    double avg_len = 0;
    for (const auto& p : paths) avg_len += p.size();
    avg_len /= paths.size();
    std::cout << avg_len << std::endl;

    // Assert reasonable performance: <0.1 ms avg for h=10, n=1000
    EXPECT_LT(avg_time_ms, 0.1);
}

// Benchmark: Time get_single_range_cover for random queries
TEST_F(TdagTest, PerformanceGetSingleRangeCover) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, (1 << perf_height_) - 1);
    const int num_queries = 1000;

    auto start = std::chrono::high_resolution_clock::now();
    //随机生成查询区间
    int total_fp_estimate = 0;  // Rough FP: cover size - query size
    for (int i = 0; i < num_queries; ++i) {
        int q_start = dis(gen);
        int q_end = std::min((1 << perf_height_) - 1, q_start + dis(gen) % 100);  // Small random queries
        auto cover = perf_root_->get_single_range_cover({q_start, q_end});
        //这里的区间长度按闭区间算：|[L,R]| = R-L+1。cover 是 TDAG 中能用一个节点覆盖查询的最小区间；
        // 由于对齐/桥接的限制，它一般会比真实查询稍大——差值可视为估计的假阳性量（over-cover）。
        total_fp_estimate += (cover.second - cover.first + 1) - (q_end - q_start + 1);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double avg_time_ms = std::chrono::duration<double, std::milli>(end - start).count() / num_queries;
    double avg_fp = static_cast<double>(total_fp_estimate) / num_queries;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Average get_single_range_cover time: " << avg_time_ms << " ms" << std::endl;
    std::cout << "Average estimated FP per query: " << avg_fp << std::endl;

    // Assert: <0.01 ms avg, FP reasonable O(R)
    EXPECT_LT(avg_time_ms, 0.01);
    EXPECT_GT(avg_fp, 0);  // Some FP expected
}

// Additional integration test: Map points to labels via descend, then query cover
TEST_F(TdagTest, IntegrationPathToCover) {
    // Simulate: For a point, get path, then query a range containing it, check cover contains point
    int test_val = 5;
    auto path = root_->descend_tree(test_val, {0, max_val_});
    std::pair<int, int> q_range = {4, 6};  // Contains 5

    auto cover = root_->get_single_range_cover(q_range);
    EXPECT_TRUE(range_contains(cover, test_val));

    std::cout << "Integration: Cover for [4,6] = [" << cover.first << "," << cover.second << "]" << std::endl;
}