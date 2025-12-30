#include <gtest/gtest.h>
#include "bplus_tree.h"
#include <vector>
#include <string>
#include <ctime>
#include <cube_code.h>

// Helper function to create a sample token (e.g., "user818_lat40.72_lon-73.99")
std::shared_ptr<std::vector<unsigned char>> create_token(const std::string& str) {
    return std::make_shared<std::vector<unsigned char>>(str.begin(), str.end());
}

// Helper function to convert date string to timestamp (start of day)
// 统一时区为utc
long long date_to_timestamp(const std::string& date) {
    struct tm tm = {};
    strptime(date.c_str(), "%Y-%m-%d", &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    time_t t = timegm(&tm); // Use timegm to handle UTC conversion，timegm使用utc时间戳，mktime使用本地时间戳
    return static_cast<long long>(t); // Convert to seconds since epoch
}

// Helper function to convert time string to 10-minute interval
//把一个包含时区的完整时间字符串（格式为 "YYYY-MM-DD HH:MM:SS+ZZZZ"）转换为该时间在一天中的第几个 10 分钟区间。
int time_to_10min_interval(const std::string& time) {
    struct tm tm = {};
    strptime(time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm); // Support +00 timezone
    return tm.tm_hour * 6 + tm.tm_min / 10; // 24 hours * 6 intervals per hour ep：18:20 → 18 小时 * 6 + 20 ÷ 10 = 108 + 2 = 110（第 110 个 10 分钟区间）
}

// Test fixture
class BPlusTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<double> min_vals = {40.5, -74.2}; // 纬度最小值
        std::vector<double> max_vals = {41.0, -73.7}; // 纬度最大值
        bpt = std::make_shared<BPlusTree>(2, min_vals, max_vals, 3);
        // bpt = std::make_shared<BPlusTree>();
        // ccs = std::make_shared<CubeCode>(2, {40.5, -74.2}, {41.0, -73.7}, 3);
    }

    std::shared_ptr<BPlusTree> bpt;
};

// Test insertion of a new day's SegmentTree
TEST_F(BPlusTreeTest, InsertNewDay) {   
    auto st = std::make_shared<SegmentTree>(144, 0.01, 100);
    long long key = date_to_timestamp("2012-04-03");
    bpt->insert(key, st);
    auto result = bpt->search(key);
    EXPECT_EQ(result, st);
    EXPECT_EQ(bpt->search(key + 86400), nullptr); // Next day should not exist
}

// Test insertion of multiple days
TEST_F(BPlusTreeTest, InsertMultipleDays) {
    auto st1 = std::make_shared<SegmentTree>(144, 0.01, 100);
    auto st2 = std::make_shared<SegmentTree>(144, 0.01, 100);
    long long key1 = date_to_timestamp("2012-04-03");
    long long key2 = date_to_timestamp("2012-04-04");
    bpt->insert(key1, st1);
    bpt->insert(key2, st2);
    auto range = bpt->rangeSearch(key1, key2);
    ASSERT_EQ(range.size(), 2);
    EXPECT_EQ(range[0].first, key1);
    EXPECT_EQ(range[0].second, st1);
    EXPECT_EQ(range[1].first, key2);
    EXPECT_EQ(range[1].second, st2);
}

// Test update in a time range
TEST_F(BPlusTreeTest, UpdateTimeRange) {
    auto st = std::make_shared<SegmentTree>(144, 0.01, 100);
    long long key = date_to_timestamp("2012-04-03");
    bpt->insert(key, st);
    auto token = create_token("ok");//token应该是关于时间戳的函数
    std::vector<double> point = {40.72, -73.99}; // Example coordinates
    auto codes = bpt->getCubeCode()->generateDataCubeCodes(point);
    bpt->update(key, 108, 108, token, codes); // 18:00 (108th 10-min interval)，2012-04-03这一天18：00-18：10这个时间间隔内的数据项
    auto result = st->query(108, 108);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], token); // Check token (keyword checked via bloom filter)
    auto candidates = st->getCandidateIntervals(108, 108, codes);
    EXPECT_FALSE(candidates.empty());
}

// Test deletion of a day's SegmentTree
TEST_F(BPlusTreeTest, DeleteDay) {
    auto st1 = std::make_shared<SegmentTree>(144, 0.01, 100);
    auto st2 = std::make_shared<SegmentTree>(144, 0.01, 100);
    long long key1 = date_to_timestamp("2012-04-03");
    long long key2 = date_to_timestamp("2012-04-04");
    bpt->insert(key1, st1);
    bpt->insert(key2, st2);
    bpt->remove(key1);
    EXPECT_EQ(bpt->search(key1), nullptr);
    EXPECT_EQ(bpt->search(key2), st2);
}

// Test SQL query simulation
TEST_F(BPlusTreeTest, SQLQuerySimulation) {
    // Insert data for multiple days
    long long start_time = date_to_timestamp("2012-04-03");
    long long end_time = date_to_timestamp("2012-04-08");
    for (long long t = start_time; t <= end_time; t += 86400) {//86400s为一天，共插入6天的数据
        bpt->insert(t, std::make_shared<SegmentTree>(144, 0.01, 100));
        auto result = bpt->search(t);
        ASSERT_NE(result, nullptr) << "Failed to find key " << t << " after insertion";
    }
    
    // Update with sample data at 18:00 on 2012-04-03
    long long key = date_to_timestamp("2012-04-03");
    std::cout << "Computed timestamp for 2012-04-03: " << key << std::endl;

    // if(key == start_time){
    //     printf("Inserting data for the first day: %lld\n", key);
    // }
    
    auto token = create_token("ok");
    std::vector<double> query_min = {40.7, -74.0};
    std::vector<double> query_max = {40.76, -73.98};
    auto codes = bpt->getCubeCode()->generateQueryCubeCodes(query_min, query_max);
    bpt->update(key, 108, 108, token, codes); // 18:00

    // Query with SQL conditions
    auto results = bpt->query_sql(
                        "2012-04-03 18:00:00+00", "2012-04-08 10:10:10+00", 
                        40.7, 40.76, -74.0, -73.98);
    
    // 验证结果
    ASSERT_EQ(results.size(), 1); // 应只匹配一个区间
    EXPECT_EQ(results[0].left, 108); // 区间起始索引
    EXPECT_EQ(results[0].right, 108); // 区间结束索引
    ASSERT_EQ(results[0].tokens.size(), 1); // 应包含1个token
    EXPECT_EQ(results[0].tokens[0], token); // token内容匹配
}

TEST_F(BPlusTreeTest, QueryAcrossMultipleDays) {
    // 插入3天的数据
    std::vector<long long> timestamps = {
        date_to_timestamp("2012-04-03"),
        date_to_timestamp("2012-04-04"),
        date_to_timestamp("2012-04-05")
    };
    
    // 为每天插入不同位置的签到数据
    std::vector<std::tuple<double, double, std::string>> locations = {
        {40.72, -73.99, "user123"},  // 第1天18:00
        {40.73, -73.98, "user456"},  // 第2天12:00
        {40.71, -73.97, "user789"}   // 第3天09:00
    };

    for (size_t i = 0; i < timestamps.size(); ++i) {
        auto st = std::make_shared<SegmentTree>(144, 0.01, 100);
        bpt->insert(timestamps[i], st);
        
        // 计算时间区间索引
        int interval = (i == 0) ? 108 : (i == 1) ? 72 : 54; // 分别对应18:00, 12:00, 09:00
        auto codes = bpt->getCubeCode()->generateDataCubeCodes(
            {std::get<0>(locations[i]), std::get<1>(locations[i])}
        );
        bpt->update(timestamps[i], interval, interval, 
                   create_token(std::get<2>(locations[i])), codes);
    }

    // 查询跨多天的数据
    auto results = bpt->query_sql(
        "2012-04-03 00:00:00+00", 
        "2012-04-05 23:59:59+00",
        40.70, 40.75,  // 覆盖所有测试点的纬度范围
        -74.0, -73.95   // 覆盖所有测试点的经度范围
    );

    // 验证结果
    ASSERT_EQ(results.size(), 3); // 应返回3个区间
    
    // 检查每个区间是否符合预期
    std::vector<int> expected_intervals = {108, 72, 54};
    for (size_t i = 0; i < results.size(); ++i) {
        EXPECT_EQ(results[i].left, expected_intervals[i]);
        EXPECT_EQ(results[i].right, expected_intervals[i]);
        ASSERT_EQ(results[i].tokens.size(), 1);
        EXPECT_EQ(
            std::string(results[i].tokens[0]->begin(), results[i].tokens[0]->end()),
            std::get<2>(locations[i])
        );
    }
}

// Test range search with updates
TEST_F(BPlusTreeTest, RangeSearchWithUpdates) {
    auto st1 = std::make_shared<SegmentTree>(144, 0.01, 100);
    auto st2 = std::make_shared<SegmentTree>(144, 0.01, 100);
    long long key1 = date_to_timestamp("2012-04-03");
    long long key2 = date_to_timestamp("2012-04-04");
    bpt->insert(key1, st1);
    bpt->insert(key2, st2);


    auto token = create_token("checkin");
    std::vector<double> point1 = {40.72, -73.99};
    std::vector<double> point2 = {40.72, -74.00};
    auto codes1 = bpt->getCubeCode()->generateDataCubeCodes(point1);
    auto codes2 = bpt->getCubeCode()->generateDataCubeCodes(point2);
    bpt->update(key1, 108, 108, token, codes1);
    bpt->update(key2, 0, 0, token, codes2);

    auto range = bpt->rangeSearch(key1, key2);
    ASSERT_EQ(range.size(), 2);
    auto results1 = range[0].second->query(108, 108);
    auto results2 = range[1].second->query(0, 0);
    ASSERT_EQ(results1.size(), 1);
    ASSERT_EQ(results2.size(), 1);
    EXPECT_EQ(results1[0], token);
    EXPECT_EQ(results2[0], token);
}

// TEST_F(BPlusTreeTest, EdgeCaseEmptyTree) {
//     EXPECT_EQ(bpt->search(0), nullptr);
//     EXPECT_TRUE(bpt->rangeSearch(0, 1000).empty());
//     EXPECT_FALSE(bpt->remove(0));
//     EXPECT_FALSE(bpt->update(0, 0, 0, create_token("test"), {"checkin"}));
// }

TEST_F(BPlusTreeTest, LargeScaleInsert) {
    long long start = date_to_timestamp("2012-01-01");
    for (int i = 0; i < 100; ++i) { //i=10的时候发生段错误
        bpt->insert(start + i * 86400, std::make_shared<SegmentTree>(144, 0.01, 100));
    }
    auto range = bpt->rangeSearch(start, start + 99 * 86400);
    EXPECT_EQ(range.size(), 100);
}

// bplus_tree_test.cpp
TEST_F(BPlusTreeTest, DisplayTreeStructure) {
    long long start = date_to_timestamp("2012-01-01");
    std::cout << "\nB+ Tree Structure after inserting 20 days:\n";
    std::cout << "=========================================\n";
    
    for (int i = 0; i < 20; ++i) {
        bpt->insert(start + i * 86400, std::make_shared<SegmentTree>(144, 0.01, 100));
    }
    
    bpt->display();
    std::cout << "=========================================\n";
    
    // 验证所有节点都能正确查找到
    for (int i = 0; i < 20; ++i) {
        auto result = bpt->search(start + i * 86400);
        EXPECT_NE(result, nullptr) << "Day " << i << " not found";
    }
    
    // 验证范围查询
    auto range = bpt->rangeSearch(start, start + 19 * 86400);
    EXPECT_EQ(range.size(), 20);
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}