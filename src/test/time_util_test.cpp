// test_time_utils.cpp
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

// g++ -std=c++17 time_util_test.cpp -o time_util_test && ./time_util_test

// 将日期字符串（YYYY-MM-DD）转换为 UTC 时间戳（秒）
static long long date_to_timestamp(const std::string& date) {
    struct tm tm = {};
    // 解析为 UTC 的日历时间（不含时分秒）
    strptime(date.c_str(), "%Y-%m-%d", &tm);
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    // GNU 扩展：使用 timegm 将 tm(UTC) 转为 time_t
    time_t t = timegm(&tm);
    return static_cast<long long>(t); // 秒
}

// 将完整时间字符串（YYYY-MM-DD HH:MM:SS+ZZZZ）转换为一天中的 10 分钟区间索引（0-143）
static int time_to_10min_interval(const std::string& time) {
    struct tm tm = {};
    // 注意：此实现解析 %z 但未根据时区对 tm_hour/min 做换算
    strptime(time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm);
    return tm.tm_hour * 6 + tm.tm_min / 10; // 24小时 * 每小时6个区间
}

// ====== 简单断言工具 ======
#define EXPECT_EQ(actual, expected) do { \
    auto _a = (actual); auto _e = (expected); \
    if (!((_a) == (_e))) { \
        std::cerr << "EXPECT_EQ failed at " << __FILE__ << ":" << __LINE__ \
                  << "\n  actual:   " << _a \
                  << "\n  expected: " << _e << std::endl; \
        std::abort(); \
    } \
} while(0)

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { \
        std::cerr << "EXPECT_TRUE failed at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while(0)

static void test_date_to_timestamp_basics() {
    // Unix epoch
    EXPECT_EQ(date_to_timestamp("1970-01-01"), 0LL);

    // 次日（UTC）+86400 秒
    EXPECT_EQ(date_to_timestamp("1970-01-02") - date_to_timestamp("1970-01-01"), 86400LL);

    // 任意一天相邻差值应为 86400
    EXPECT_EQ(date_to_timestamp("2024-08-21") - date_to_timestamp("2024-08-20"), 86400LL);

    // 闰年：2020-02-29 存在，2月29 -> 3月1 相差 86400
    EXPECT_EQ(date_to_timestamp("2020-03-01") - date_to_timestamp("2020-02-29"), 86400LL);

    // 世纪闰年：2000 是闰年；2月28 -> 3月1 相差 2 天（跨过 2/29）
    EXPECT_EQ(date_to_timestamp("2000-03-01") - date_to_timestamp("2000-02-28"), 2 * 86400LL);

    // 非闰年：1900 不是闰年；2月28 -> 3月1 仅 1 天
    EXPECT_EQ(date_to_timestamp("1900-03-01") - date_to_timestamp("1900-02-28"), 86400LL);
}

static void test_time_to_10min_interval_edges() {
    // 00:00:00 -> 区间 0
    EXPECT_EQ(time_to_10min_interval("2024-08-20 00:00:00+0000"), 0);

    // 00:09:59 -> 仍在区间 0
    EXPECT_EQ(time_to_10min_interval("2024-08-20 00:09:59+0000"), 0);

    // 00:10:00 -> 进入区间 1
    EXPECT_EQ(time_to_10min_interval("2024-08-20 00:10:00+0000"), 1);

    // 23:59:59 -> 最后一个区间 143
    EXPECT_EQ(time_to_10min_interval("2024-08-20 23:59:59+0000"), 24*6 - 1);
}

static void test_time_to_10min_interval_various() {
    // 任意小时：08:00 -> 8*6 = 48
    EXPECT_EQ(time_to_10min_interval("2024-08-20 08:00:00+0000"), 48);

    // 任意分钟：14:37 -> 14*6 + 3 = 87
    EXPECT_EQ(time_to_10min_interval("2024-08-20 14:37:00+0000"), 14*6 + 3);

    // 带不同时区标注的字符串（实现未根据 %z 调整小时，按字符串中 HH:MM 直接分段）
    EXPECT_EQ(time_to_10min_interval("2024-08-20 08:05:00+0530"), 48); // 08:05 -> 48
    EXPECT_EQ(time_to_10min_interval("2024-08-20 21:59:59-0700"), 21*6 + 5); // 21:59 -> 131
}

int main() {
    std::cout << "Running tests...\n";

    test_date_to_timestamp_basics();
    test_time_to_10min_interval_edges();
    test_time_to_10min_interval_various();

    std::cout << "-----All tests passed----- \n";
    return 0;
}
