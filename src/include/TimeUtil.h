#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <string>
#include <ctime>

class TimeUtil {
public:
    // 将日期字符串（YYYY-MM-DD）转换为 UTC 时间戳（秒）
    static long long date_to_timestamp(const std::string& date) {
        struct tm tm = {};
        strptime(date.c_str(), "%Y-%m-%d", &tm);
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        time_t t = timegm(&tm); // 使用 timegm 处理 UTC 时间戳
        return static_cast<long long>(t); // 转换为秒
    }

    // 将完整时间字符串（YYYY-MM-DD HH:MM:SS+ZZZZ）转换为一天中的 10 分钟区间索引（0-143）
    static int time_to_10min_interval(const std::string& time) {
        struct tm tm = {};
        strptime(time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm); // 支持时区
        return tm.tm_hour * 6 + tm.tm_min / 10; // 24小时 * 每小时6个区间
    }

    // 将完整时间字符串（YYYY-MM-DD HH:MM:SS）转换为 UTC 时间戳（秒）
    static long long to_timestamp(const std::string& time_str) {
        struct tm tm = {};
        strptime(time_str.c_str(), "%Y-%m-%d %H:%M:%S", &tm); // 支持时区
        
        long long timestamp = static_cast<long long>(timegm(&tm));
        return timestamp;
    }
};

#endif // TIME_UTIL_H