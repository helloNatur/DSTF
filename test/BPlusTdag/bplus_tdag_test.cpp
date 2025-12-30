#include <gtest/gtest.h>
#include "bplus_tdag.h"
#include <vector>
#include <string>
#include <ctime>
#include <cube_code.h>

// Helper function to create a sample token (e.g., "user818_lat40.72_lon-73.99")
std::shared_ptr<std::vector<unsigned char>> create_token(const std::string& str) {
    return std::make_shared<std::vector<unsigned char>>(str.begin(), str.end());
}

// Test fixture
class BPlusTdagTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<double> min_vals = {40.5, -74.2}; // 纬度最小值
        std::vector<double> max_vals = {41.0, -73.7}; // 纬度最大值
        bpt = std::make_shared<BPlusTdag>(2, min_vals, max_vals, 3);
        // bpt = std::make_shared<BPlusTree>();
        // ccs = std::make_shared<CubeCode>(2, {40.5, -74.2}, {41.0, -73.7}, 3);
    }

    std::shared_ptr<BPlusTdag> bpt;
};


// perf_height_, fp_, capacity_, seed_
// Test insertion of a new day's BPlusTdag
TEST_F(BPlusTdagTest, InsertNewDay) {   
    auto tdag = TdagBF::initialize(8, 0.01, 10, 0);
    long long key = TimeUtil::date_to_timestamp("2012-04-03");
    bpt->insert(key,tdag);
    auto result = bpt->search(key);
    EXPECT_EQ(result, tdag);
    EXPECT_EQ(bpt->search(key + 86400), nullptr); // Next day should not exist
}

// 测试2: 范围搜索功能
TEST_F(BPlusTdagTest, RangeSearchMultipleDays) {
    long long day1_ts = TimeUtil::date_to_timestamp("2025-10-17");
    long long day2_ts = TimeUtil::date_to_timestamp("2025-10-18");
    auto tdag1 = TdagBF::initialize(8, 0.01, 100, 0);
    auto tdag2 = TdagBF::initialize(8, 0.01, 100, 0);

    bpt->insert(day1_ts, tdag1);
    bpt->insert(day2_ts, tdag2);

    auto range_result = bpt->rangeSearch(day1_ts, day2_ts);
    ASSERT_EQ(range_result.size(), 2);
    EXPECT_EQ(range_result[0].first, day1_ts);
    EXPECT_EQ(range_result[0].second, tdag1);
    EXPECT_EQ(range_result[1].first, day2_ts);
    EXPECT_EQ(range_result[1].second, tdag2);
}

// 测试3: 单点更新功能
TEST_F(BPlusTdagTest, UpdatePointAndVerify) {
    long long day_ts = TimeUtil::date_to_timestamp("2025-10-17");
    int interval = 110; // 对应 18:20 - 18:30

    // 定义一个空间点并生成其 cube code
    std::vector<double> point = {40.72, -73.99};
    auto codes = bpt->getCubeCode()->generateDataCubeCodes(point);

    // 使用 update_point 插入数据 (该函数会自动创建当天的 TDAG)
    bool success = bpt->update_point(day_ts, interval, codes);
    ASSERT_TRUE(success);

    // 验证数据是否真的被写入
    auto tdag = bpt->search(day_ts);
    ASSERT_NE(tdag, nullptr);
    
    // 使用 get_single_range_cover 检查是否能找到覆盖
    auto cover = tdag->get_single_range_cover({interval, interval}, codes);
    EXPECT_NE(cover.first, -1); // 应该能找到一个有效的覆盖
    // 验证覆盖区间确实包含了查询区间
    EXPECT_LE(cover.first, interval);
    EXPECT_GE(cover.second, interval);
}

// 测试4: 核心功能 - 端到端的时空查询
TEST_F(BPlusTdagTest, QueryTimeCandidates_EndToEnd) {
    // --- 数据准备 ---
    long long day1_ts = TimeUtil::date_to_timestamp("2025-10-17");
    long long day2_ts = TimeUtil::date_to_timestamp("2025-10-18");

    // 地点A (在查询范围内)
    std::vector<double> pointA = {40.72, -73.99}; 
    auto codesA = bpt->getCubeCode()->generateDataCubeCodes(pointA);

    // 地点B (在查询范围外)
    std::vector<double> pointB = {40.90, -74.10};
    auto codesB = bpt->getCubeCode()->generateDataCubeCodes(pointB);
    
    // 在第一天的 18:20 插入地点A的数据
    bpt->update_point(day1_ts, 110, codesA);
    // 在第一天的 10:00 插入地点B的数据 (这个应该查询不到)
    bpt->update_point(day1_ts, 60, codesB);
    
    // 在第二天的 09:30 插入地点A的数据
    bpt->update_point(day2_ts, 57, codesA);

    // --- 执行查询 ---
    // 查询时间范围: 2025-10-17 12:00:00 到 2025-10-18 10:00:00
    // 查询空间范围: 包含地点A，但不包含地点B
    auto candidates = bpt->query_time_candidates(
        "2025-10-17 12:00:00", "2025-10-18 10:00:00",
        40.7, 40.8, -74.0, -73.9
    );

    // --- 验证结果 ---
    // 预期应返回2个候选时间段
    ASSERT_EQ(candidates.size(), 2);

    // 验证第一个候选段 (来自第一天)
    // 查询从 12:00 (interval 72) 开始, 数据在 18:20 (interval 110)
    EXPECT_EQ(candidates[0].day_ts, day1_ts);
    EXPECT_LE(candidates[0].left_interval, 110);
    EXPECT_GE(candidates[0].right_interval, 110);

    // 验证第二个候选段 (来自第二天)
    // 查询到 10:00 (interval 60) 结束, 数据在 09:30 (interval 57)
    EXPECT_EQ(candidates[1].day_ts, day2_ts);
    EXPECT_LE(candidates[1].left_interval, 57);
    EXPECT_GE(candidates[1].right_interval, 57);
}

//终止日 end_interval 判断错误（会把“超出终止分钟”的记录也算进来）
TEST_F(BPlusTdagTest, EndInterval_ShouldClampOnLastDayOnly) {
    // 两天：第二天放两条记录，一条在允许窗口内(09:30=57)，一条超出窗口(20:00=120)
    long long d1 = TimeUtil::date_to_timestamp("2025-10-18");
    long long d2 = TimeUtil::date_to_timestamp("2025-10-19");
    auto codes = bpt->getCubeCode()->generateDataCubeCodes({40.72, -73.99});

    bpt->update_point(d1, 110, codes);   // 第一天 18:20
    bpt->update_point(d2, 57,  codes);   // 第二天 09:30   ← 在终止窗口内
    bpt->update_point(d2, 120, codes);   // 第二天 20:00   ← 超出终止窗口

    // 查询：从 10/18 12:00 到 10/19 10:00（end_interval = 60）
    auto cand = bpt->query_time_candidates(
        "2025-10-18 12:00:00", "2025-10-19 10:00:00", 40.7, 40.8, -74.0, -73.9
    );

    // 预期：只返回 2 段（d1 的一段，d2 的 09:30 一段）；当前实现常返回 3 段（把 20:00 也算进来了）
    // 为避免依赖排序，这里统计每一天返回的段数
    int d1_cnt = 0, d2_cnt = 0;
    for (auto &c : cand) {
        if (c.day_ts == d1) d1_cnt++;
        if (c.day_ts == d2) d2_cnt++;
    }
    ASSERT_EQ(d1_cnt, 1) << "First day should contribute exactly one candidate.";
    ASSERT_EQ(d2_cnt, 1) << "Last day should clamp to end_interval; extra segment indicates a bug.";
}

// 测试5: 压力测试，验证B+树分裂逻辑
TEST_F(BPlusTdagTest, LargeScaleInsertAndRangeSearch) {
    long long start_ts = TimeUtil::date_to_timestamp("2025-01-01");
    int num_days = 100;

    for (int i = 0; i < num_days; ++i) {
        // order_=3，插入超过3个就会触发分裂
        bpt->insert(start_ts + i * 86400, TdagBF::initialize(8, 0.01, 100, 0));
    }

    auto range = bpt->rangeSearch(start_ts, start_ts + (num_days - 1) * 86400);
    EXPECT_EQ(range.size(), num_days);
}

//触发“内部结点分裂上升键越界”
TEST_F(BPlusTdagTest, InternalSplit_PromoteKeyOutOfBounds) {
    // 用较多的 key 逼出父结点分裂
    long long start_ts = TimeUtil::date_to_timestamp("2025-01-01");
    const int num_days = 200; // 取大一点，稳定触发内部节点分裂
    for (int i = 0; i < num_days; ++i) {
        bpt->insert(start_ts + 86400 * i, TdagBF::initialize(8, 0.01, 10, 0));
    }
    auto range = bpt->rangeSearch(start_ts, start_ts + 86400 * (num_days - 1));
    // 预期应等于 num_days；当前实现常出现 < num_days（或直接崩）
    ASSERT_EQ(range.size(), static_cast<size_t>(num_days)) 
        << "Internal split likely read promote key out-of-bounds.";
}





TEST_F(BPlusTdagTest, RemoveOnEmptyTree_ShouldNotCrash) {
    // 空树直接删除任意 key —— 预期应安全返回 false
    long long k = TimeUtil::date_to_timestamp("2012-01-01");
    // 如果当前实现崩了，说明 remove 里缺少空指针保护
    bool ok = bpt->remove(k);
    // 我们这里并不强求 true/false 语义，只要不崩即可；若你修了实现，建议返回 false
    SUCCEED() << "remove() on empty tree should not crash.";
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}