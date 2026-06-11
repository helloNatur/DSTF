#pragma once

#include "TimeUtil.h"
#include "logarithmic_src_i1_index.hpp"
#include "standard_emm.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct I1SpatiotemporalPoint {
    std::string time_str;
    long long utc_timestamp;
    double latitude;
    double longitude;
    int record_id;

    I1SpatiotemporalPoint(std::string time,
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

struct I1QueryTimings {
    double query_gen_ms = 0.0;
    double eval_ms = 0.0;
    double result_decrypt_ms = 0.0;
};

inline std::size_t I1GetEnvSizeT(const char* name, std::size_t fallback) {
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

inline int I1GetEnvInt(const char* name, int fallback) {
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

inline std::vector<I1SpatiotemporalPoint> I1LoadSpatiotemporalData(
    const std::string& filepath,
    std::size_t limit_n = SIZE_MAX) {
    std::vector<I1SpatiotemporalPoint> data;
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
        size_t start = 0;
        size_t end = 0;
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

inline int I1GlobalIntervalAttr(long long min_day_ts, const I1SpatiotemporalPoint& point) {
    long long day_ts = TimeUtil::date_to_timestamp(point.time_str.substr(0, 10));
    int day_offset = static_cast<int>((day_ts - min_day_ts) / 86400);
    return day_offset * 144 + TimeUtil::time_to_10min_interval(point.time_str);
}

inline int I1GlobalIntervalAttr(long long min_day_ts,
                                const std::string& time_str) {
    long long day_ts = TimeUtil::date_to_timestamp(time_str.substr(0, 10));
    int day_offset = static_cast<int>((day_ts - min_day_ts) / 86400);
    return day_offset * 144 + TimeUtil::time_to_10min_interval(time_str);
}

inline std::vector<int> I1RunQuery(
    const LogarithmicSrcI1Index& index,
    StandardEMM& emm_engine,
    int attr_start,
    int attr_end,
    I1QueryTimings& timings) {
    auto query_gen_start = std::chrono::high_resolution_clock::now();
    auto i1_labels = index.getI1QueryLabels(attr_start, attr_end);
    auto i1_tokens = emm_engine.generateTokens(i1_labels);
    auto query_gen_end = std::chrono::high_resolution_clock::now();
    timings.query_gen_ms +=
        std::chrono::duration<double, std::milli>(query_gen_end - query_gen_start).count();

    auto eval_start = std::chrono::high_resolution_clock::now();
    auto enc_i1 = emm_engine.query(i1_tokens);
    auto eval_end = std::chrono::high_resolution_clock::now();
    timings.eval_ms += std::chrono::duration<double, std::milli>(eval_end - eval_start).count();

    auto decrypt_start = std::chrono::high_resolution_clock::now();
    auto range_ids = emm_engine.decryptResults(enc_i1);
    auto decrypt_end = std::chrono::high_resolution_clock::now();
    timings.result_decrypt_ms +=
        std::chrono::duration<double, std::milli>(decrypt_end - decrypt_start).count();

    query_gen_start = std::chrono::high_resolution_clock::now();
    auto pos_ranges = index.filterAndMergePositionRanges(range_ids, attr_start, attr_end);
    auto i2_labels = index.getI2QueryLabels(pos_ranges);
    auto i2_tokens = emm_engine.generateTokens(i2_labels);
    query_gen_end = std::chrono::high_resolution_clock::now();
    timings.query_gen_ms +=
        std::chrono::duration<double, std::milli>(query_gen_end - query_gen_start).count();

    eval_start = std::chrono::high_resolution_clock::now();
    auto enc_i2 = emm_engine.query(i2_tokens);
    eval_end = std::chrono::high_resolution_clock::now();
    timings.eval_ms += std::chrono::duration<double, std::milli>(eval_end - eval_start).count();

    decrypt_start = std::chrono::high_resolution_clock::now();
    auto result_ids = emm_engine.decryptResults(enc_i2);
    decrypt_end = std::chrono::high_resolution_clock::now();
    timings.result_decrypt_ms +=
        std::chrono::duration<double, std::milli>(decrypt_end - decrypt_start).count();

    return result_ids;
}

inline std::set<int> I1GroundTruth(
    const std::vector<I1SpatiotemporalPoint>& points,
    long long start_ts,
    long long end_ts,
    double lat_min,
    double lat_max,
    double lon_min,
    double lon_max) {
    std::set<int> truth;
    for (const auto& point : points) {
        if (point.utc_timestamp >= start_ts && point.utc_timestamp <= end_ts &&
            point.latitude >= lat_min && point.latitude <= lat_max &&
            point.longitude >= lon_min && point.longitude <= lon_max) {
            truth.insert(point.record_id);
        }
    }
    return truth;
}
