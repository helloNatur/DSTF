//分别有三部分token，一部分属于范围查询全部变为点查询，也就是时间范围变为点查询，
//一部分属于经纬度这个二维范围也变为点查询，但是这里已经包含在bf中
//最后一部分是keyword2的token

#include <gtest/gtest.h>
#include "HashSetup_JXTp.hpp"
#include "IdTokenTestMetrics.hpp"
#include "Setup_JXTp.hpp"
#include "Server_JXTp.hpp"
#include "adaptive_time_bucket.hpp"
#include "cube_code.h"
#include "bplus_tree.h"
#include "TimeUtil.h"
#include "Hash.hpp"
#include "AESUtil.hpp"
#include "tool.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>
#include <string>
#include <set>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

namespace {

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

std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
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

std::string RangeTimeKey(long long bucket_id, int local_slot) {
    return "bucket_" + std::to_string(bucket_id) + "_slot_" +
           std::to_string(local_slot);
}

struct HashRangeRecord {
    int record_id;
    long long timestamp;
    std::vector<std::string> codes;
};

struct PlainRecord {
    std::string join_value;
    std::string id;
    long long timestamp;
    double lat;
    double lon;
};

std::string JoinKeywordValue(std::string_view join_attr, std::string_view keyword) {
    const std::string attr{join_attr};
    const std::string key{keyword};
    if (key.rfind(attr, 0) == 0) {
        return key.substr(attr.size());
    }
    return key;
}

std::vector<PlainRecord> LoadPlainRecords(const std::string& path, std::size_t limit_n) {
    std::ifstream file{path};
    if (!file) {
        throw std::runtime_error("Failed to open " + path);
    }

    std::vector<PlainRecord> records;
    records.reserve(limit_n);
    std::string line;
    int counter = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (counter > static_cast<int>(limit_n)) {
            break;
        }
        auto fields = SplitCsvLine(line);
        if (counter != 0 && fields.size() > 8) {
            records.push_back({
                fields[0],
                fields[8],
                TimeUtil::to_timestamp(fields[7]),
                std::stod(fields[4]),
                std::stod(fields[5]),
            });
        }
        ++counter;
    }
    return records;
}

std::set<std::string> BuildUnifiedGroundTruth(
    const std::string& table1_path,
    const std::string& table2_path,
    std::size_t limit_n,
    const std::string& start_time,
    const std::string& end_time,
    double lat_min,
    double lat_max,
    double lon_min,
    double lon_max,
    std::string_view join_attr,
    std::string_view keyword2) {
    const long long start_ts = TimeUtil::to_timestamp(start_time);
    const long long end_ts = TimeUtil::to_timestamp(end_time);
    const std::string join_value = JoinKeywordValue(join_attr, keyword2);

    std::unordered_set<std::string> table2_join_values;
    for (const auto& row : LoadPlainRecords(table2_path, limit_n)) {
        if (row.join_value == join_value) {
            table2_join_values.insert(row.join_value);
        }
    }

    std::set<std::string> expected;
    if (table2_join_values.empty()) {
        return expected;
    }
    for (const auto& row : LoadPlainRecords(table1_path, limit_n)) {
        if (row.timestamp < start_ts || row.timestamp > end_ts) {
            continue;
        }
        if (row.lat < lat_min || row.lat > lat_max ||
            row.lon < lon_min || row.lon > lon_max) {
            continue;
        }
        if (table2_join_values.find(row.join_value) != table2_join_values.end()) {
            expected.insert(row.id);
        }
    }
    return expected;
}

void PrintUnifiedGroundTruthStats(const std::set<std::string>& decrypted_range,
                                  const std::set<std::string>& decrypted_stag1,
                                  const std::set<std::string>& expected) {
    std::set<std::string> final_result;
    std::set_intersection(decrypted_range.begin(), decrypted_range.end(),
                          decrypted_stag1.begin(), decrypted_stag1.end(),
                          std::inserter(final_result, final_result.begin()));

    std::set<std::string> true_positive;
    std::set_intersection(final_result.begin(), final_result.end(),
                          expected.begin(), expected.end(),
                          std::inserter(true_positive, true_positive.begin()));
    const std::size_t false_positive = final_result.size() - true_positive.size();
    const std::size_t false_negative = expected.size() - true_positive.size();
    const double fp_rate = final_result.empty()
        ? 0.0
        : static_cast<double>(false_positive) / static_cast<double>(final_result.size());

    std::cout << std::fixed << std::setprecision(6)
              << "[Unified Ground Truth] expected=" << expected.size()
              << ", final_result=" << final_result.size()
              << ", true_positive=" << true_positive.size()
              << ", false_positive=" << false_positive
              << ", false_negative=" << false_negative
              << ", fp_rate=" << fp_rate << "\n";
}

std::pair<std::vector<double>, std::vector<double>> BoundsFromCsv(
    const std::string& path, std::size_t limit_n) {
    std::ifstream file{path};
    if (!file) {
        throw std::runtime_error("Failed to open " + path);
    }
    double min_lat = std::numeric_limits<double>::max();
    double min_lon = std::numeric_limits<double>::max();
    double max_lat = std::numeric_limits<double>::lowest();
    double max_lon = std::numeric_limits<double>::lowest();
    std::string line;
    int counter = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (counter > static_cast<int>(limit_n)) {
            break;
        }
        auto record = SplitCsvLine(line);
        if (counter != 0 && record.size() > 5) {
            const double lat = std::stod(record[4]);
            const double lon = std::stod(record[5]);
            min_lat = std::min(min_lat, lat);
            min_lon = std::min(min_lon, lon);
            max_lat = std::max(max_lat, lat);
            max_lon = std::max(max_lon, lon);
        }
        ++counter;
    }
    const double lat_pad = std::max(1e-6, (max_lat - min_lat) * 0.001);
    const double lon_pad = std::max(1e-6, (max_lon - min_lon) * 0.001);
    return {{min_lat - lat_pad, min_lon - lon_pad},
            {max_lat + lat_pad, max_lon + lon_pad}};
}

struct QueryDefaults {
    std::string start_time;
    std::string end_time;
    double lat_min;
    double lat_max;
    double lon_min;
    double lon_max;
};

QueryDefaults QueryDefaultsFromCsv(const std::string& path, std::size_t limit_n) {
    std::ifstream file{path};
    if (!file) {
        throw std::runtime_error("Failed to open " + path);
    }

    double min_lat = std::numeric_limits<double>::max();
    double min_lon = std::numeric_limits<double>::max();
    double max_lat = std::numeric_limits<double>::lowest();
    double max_lon = std::numeric_limits<double>::lowest();
    std::string center_time;
    double center_lat = 0.0;
    double center_lon = 0.0;
    bool have_center = false;
    std::size_t valid_count = 0;
    const std::size_t target_center = std::max<std::size_t>(1, limit_n / 2);

    std::string line;
    int counter = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (counter > static_cast<int>(limit_n)) {
            break;
        }
        auto record = SplitCsvLine(line);
        if (counter != 0 && record.size() > 7) {
            const std::string& time_str = record[7];
            const double lat = std::stod(record[4]);
            const double lon = std::stod(record[5]);
            min_lat = std::min(min_lat, lat);
            min_lon = std::min(min_lon, lon);
            max_lat = std::max(max_lat, lat);
            max_lon = std::max(max_lon, lon);
            ++valid_count;
            if (!have_center || valid_count == target_center) {
                center_time = time_str;
                center_lat = lat;
                center_lon = lon;
                have_center = true;
            }
        }
        ++counter;
    }
    if (!have_center) {
        throw std::runtime_error("No valid spatiotemporal rows in " + path);
    }

    const double lat_pad = std::max(1e-6, (max_lat - min_lat) * 0.001);
    const double lon_pad = std::max(1e-6, (max_lon - min_lon) * 0.001);
    min_lat -= lat_pad;
    min_lon -= lon_pad;
    max_lat += lat_pad;
    max_lon += lon_pad;

    const double lat_delta = std::max(1e-6, (max_lat - min_lat) * 0.02);
    const double lon_delta = std::max(1e-6, (max_lon - min_lon) * 0.02);
    const std::string fallback_day = center_time.substr(0, 10);

    return {
        fallback_day + " 00:00:00+00",
        fallback_day + " 23:59:59+00",
        std::max(min_lat, center_lat - lat_delta),
        std::min(max_lat, center_lat + lat_delta),
        std::max(min_lon, center_lon - lon_delta),
        std::min(max_lon, center_lon + lon_delta),
    };
}

}  // namespace

class JXTpTest : public ::testing::Test {
protected:
    void SetUp() override {
        K_token = "89b7a92966f6eb32";
        K_w = "7975922666f6eb02";
        K_h = "9874a22554e7db85";
        K_aes = "8975924566f6e252";

        key_colnum = 7;
        join_column = 1;
        // record_num controls the input filename table*_k7_j1_<record_num>.csv.
        record_num = GetEnvInt("JXT2_RECORD_N", 573703);
        condition = "";
        limit_n = GetEnvSizeT("JXT2_LIMIT_N", static_cast<std::size_t>(record_num));
        query_runs = GetEnvInt("JXT2_QUERY_RUNS", 100);
        join_attr1 = "userid";
        join_attr2 = "userid";

        std::cout << "------------ HashSetup JXT+ setup ------------\n";
        auto setup_start = std::chrono::high_resolution_clock::now();
        // 初始化 table_1 和 table_2
        const std::size_t legacy_hash_index_capacity = GetEnvSizeT("JXT2_HASH_DAY_CAPACITY", 8);
        const std::size_t hash_pool_mb = GetEnvSizeT("JXT2_HASH_POOL_MB", 256);
        table1_path = std::string(DATA_DIR) +
            "/table1/table1_k" + std::to_string(key_colnum) +
            "_j" + std::to_string(join_column) + "_" +
            std::to_string(record_num) + condition + ".csv";
        table2_path = std::string(DATA_DIR) +
            "/table2/table2_k" + std::to_string(key_colnum) +
            "_j" + std::to_string(join_column) + "_" +
            std::to_string(record_num) + condition + ".csv";
        const auto auto_bounds = BoundsFromCsv(table1_path, limit_n);
        const std::vector<double> min_bounds = {
            GetEnvDouble("JXT2_BOUND_LAT_MIN", auto_bounds.first[0]),
            GetEnvDouble("JXT2_BOUND_LON_MIN", auto_bounds.first[1])
        };
        const std::vector<double> max_bounds = {
            GetEnvDouble("JXT2_BOUND_LAT_MAX", auto_bounds.second[0]),
            GetEnvDouble("JXT2_BOUND_LON_MAX", auto_bounds.second[1])
        };
        constexpr int cube_code_level = 8;
        const auto query_defaults = QueryDefaultsFromCsv(table1_path, limit_n);
        table_1 = std::make_shared<HashSetup_JXTp>(
            1, key_colnum, join_column, record_num, condition, limit_n,
            true, legacy_hash_index_capacity, hash_pool_mb * 1024ULL * 1024ULL,
            min_bounds, max_bounds, cube_code_level);
        table_1->construct();
        auto& tset = table_1->getTset();
        table_2 = std::make_shared<HashSetup_JXTp>(
            2, key_colnum, join_column, record_num, condition, limit_n,
            false, legacy_hash_index_capacity, hash_pool_mb * 1024ULL * 1024ULL,
            min_bounds, max_bounds, cube_code_level);
        table_2->construct();
        tset.insert(table_2->getTset().begin(), table_2->getTset().end());
        

        start_time = GetEnvString("JXT2_QUERY_START", query_defaults.start_time);
        end_time = GetEnvString("JXT2_QUERY_END", query_defaults.end_time);
        lat_min = GetEnvDouble("JXT2_QUERY_LAT_MIN", query_defaults.lat_min);
        lat_max = GetEnvDouble("JXT2_QUERY_LAT_MAX", query_defaults.lat_max);
        lon_min = GetEnvDouble("JXT2_QUERY_LON_MIN", query_defaults.lon_min);
        lon_max = GetEnvDouble("JXT2_QUERY_LON_MAX", query_defaults.lon_max);
        keyword2_storage = GetEnvString(
            "JXT2_JOIN_KEYWORD",
            GetEnvString("JXT2_KEYWORD2", "userid818"));
        keyword2 = keyword2_storage;

        //TODO:server只接收t1
        server = std::make_shared<Server_JXTp>(tset, table_1->getF(), table_1->getCset(), table_2->getF(), table_2->getCset());
        auto setup_end = std::chrono::high_resolution_clock::now();
        auto setup_duration = std::chrono::duration_cast<std::chrono::milliseconds>(setup_end - setup_start);
        std::cout << "Setup time: " << setup_duration.count() << " ms\n";
        std::cout << "Build time: " << setup_duration.count() << " ms\n";
        std::cout << "Loaded " << limit_n << " data points.\n";

        size_t tset_bytes = 0;
        for (const auto& [token, lists] : tset) {
            tset_bytes += token.size();
            for (const auto& l : lists) {
                tset_bytes += l.size();
            }
        }
        std::cout << "[Index Size] tset: " << tset_bytes / 1024.0 / 1024.0 << " MB\n";

        auto cset = table_1->getCset();
        cset.insert(table_2->getCset().begin(), table_2->getCset().end());
        auto f1 = table_1->getF();
        auto f2 = table_2->getF(); 

        size_t cset_bytes = 0;
        for (const auto& [key, vec] : cset) {
            cset_bytes += sizeof(key);
            for (const auto& ct : vec) cset_bytes += ct.size();
        }
        std::cout << "[Index Size] cset: " << cset_bytes / 1024.0 / 1024.0 << " MB\n";

        
        size_t bloom1_bytes = 0;
        for (auto &chunk : f1.getData()) bloom1_bytes += chunk.size();

        size_t bloom2_bytes = 0;
        for (auto &chunk : f2.getData()) bloom2_bytes += chunk.size();

        std::cout << "Bloom1 size = " << bloom1_bytes / 1024.0 / 1024.0 << " MB\n";
        std::cout << "Bloom2 size = " << bloom2_bytes / 1024.0 / 1024.0 << " MB\n";
        std::cout << "Bloom total size = " 
                << (bloom1_bytes + bloom2_bytes) / 1024.0 / 1024.0 
                << " MB\n";

        size_t range_index_bytes = table_1->getRangeIndexStorageBytes() + table_2->getRangeIndexStorageBytes();
        std::cout << "[RangeIndex] Adaptive buckets table1/table2: "
                  << table_1->getDayCount() << "/" << table_2->getDayCount() << "\n";
        std::cout << "[RangeIndex] Hash indexed points table1/table2: "
                  << table_1->getIndexedPointCount() << "/" << table_2->getIndexedPointCount() << "\n";
        std::cout << "[Index Size] range_index_approx: "
                  << range_index_bytes / 1024.0 / 1024.0 << " MB\n";
        const auto total_index_bytes =
            tset_bytes + cset_bytes + bloom1_bytes + bloom2_bytes + range_index_bytes;
        std::cout << "[Index Size] total_with_range_index: "
                  << total_index_bytes / 1024.0 / 1024.0
                  << " MB\n";
        std::cout << "[Storage] Total Index Size: "
                  << total_index_bytes / 1024.0
                  << " KB\n";

    }

    void buildAdaptiveHashIndex(const std::string& table1_path) {
        const auto auto_bounds = BoundsFromCsv(table1_path, limit_n);
        const std::vector<double> min_bounds = {
            GetEnvDouble("JXT2_BOUND_LAT_MIN", auto_bounds.first[0]),
            GetEnvDouble("JXT2_BOUND_LON_MIN", auto_bounds.first[1])
        };
        const std::vector<double> max_bounds = {
            GetEnvDouble("JXT2_BOUND_LAT_MAX", auto_bounds.second[0]),
            GetEnvDouble("JXT2_BOUND_LON_MAX", auto_bounds.second[1])
        };
        constexpr int cube_code_level = 8;
        hash_cube_code = std::make_shared<CubeCode>(
            2, min_bounds, max_bounds, cube_code_level);
        std::cout << std::fixed << std::setprecision(8)
                  << "[AdaptiveHashIndex Bounds] lat=[" << min_bounds[0] << "," << max_bounds[0]
                  << "], lon=[" << min_bounds[1] << "," << max_bounds[1]
                  << "], cube_level=" << cube_code_level << "\n";

        std::ifstream file{table1_path};
        if (!file) {
            throw std::runtime_error("Failed to open " + table1_path);
        }

        std::string line;
        int counter = 0;
        std::vector<HashRangeRecord> range_records;
        range_records.reserve(limit_n);
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (counter > static_cast<int>(limit_n)) {
                break;
            }

            auto record = SplitCsvLine(line);
            if (counter != 0 && record.size() > 8) {
                const std::string& time_str = record[7];
                const long long timestamp = TimeUtil::to_timestamp(time_str);
                std::vector<double> point = {std::stod(record[4]), std::stod(record[5])};
                auto codes = DeduplicateCodesPreserveOrder(
                    hash_cube_code->generateDataCubeCodes(point));
                range_records.push_back({counter, timestamp, std::move(codes)});
            }
            ++counter;
        }

        std::vector<AdaptiveTimePointRef> time_points;
        time_points.reserve(range_records.size());
        for (const auto& record : range_records) {
            time_points.push_back({record.record_id, record.timestamp});
        }
        hash_time_config = AdaptiveTimeBucketBuilder::configFromEnv();
        hash_time_builder = AdaptiveTimeBucketBuilder(hash_time_config);
        hash_time_builder.build(std::move(time_points));
        hash_time_builder.printStats(std::cout);

        hash_bucket_segment_trees.clear();
        hash_bucket_segment_trees.resize(hash_time_builder.buckets().size());
        for (const auto& bucket : hash_time_builder.buckets()) {
            const int tree_size = std::max<int>(
                1, static_cast<int>(bucket.occupied_abs_slots.size()));
            hash_bucket_segment_trees[static_cast<std::size_t>(bucket.bucket_id)] =
                std::make_shared<SegmentTree>(tree_size, 0.001, 442);
        }

        for (const auto& record : range_records) {
            const int bucket_id = hash_time_builder.bucketIdForRecord(record.record_id);
            if (bucket_id < 0 ||
                static_cast<std::size_t>(bucket_id) >= hash_time_builder.buckets().size()) {
                throw std::runtime_error("Adaptive bucket lookup failed for record_id " +
                                         std::to_string(record.record_id));
            }
            const auto& bucket =
                hash_time_builder.buckets()[static_cast<std::size_t>(bucket_id)];
            const long long abs_slot = hash_time_builder.absSlotForTimestamp(record.timestamp);
            const int local_slot = bucket.localSlotForAbsSlot(abs_slot);
            if (local_slot < 0) {
                throw std::runtime_error("Adaptive local slot lookup failed for record_id " +
                                         std::to_string(record.record_id));
            }

            const std::string time_key = RangeTimeKey(bucket_id, local_slot);
            const std::string token_input =
                std::string{K_token} + time_key + std::string{join_attr1} + "1";
            auto token =
                std::make_shared<std::vector<unsigned char>>(Hash::Get_SHA_256(token_input));
            hash_bucket_segment_trees[static_cast<std::size_t>(bucket_id)]->update_deferred(
                local_slot, token, record.codes);
        }

        for (auto& segment_tree : hash_bucket_segment_trees) {
            if (segment_tree) {
                segment_tree->finalize_bloom_filters();
            }
        }
        std::cout << "[AdaptiveHashIndex] Indexed points: " << range_records.size() << "\n";
        std::cout << "[AdaptiveHashIndex] Active buckets: "
                  << hash_time_builder.buckets().size() << "\n";
    }

    std::vector<SegmentTree::IntervalResult> getHashCandidateIntervals(
        const std::string& start,
        const std::string& end,
        double query_lat_min,
        double query_lat_max,
        double query_lon_min,
        double query_lon_max) const {
        std::vector<SegmentTree::IntervalResult> result;
        const long long start_ts = TimeUtil::to_timestamp(start);
        const long long end_ts = TimeUtil::to_timestamp(end);

        std::vector<double> query_min = {query_lat_min, query_lon_min};
        std::vector<double> query_max = {query_lat_max, query_lon_max};
        auto query_codes = table_1->getCubeCode()->generateQueryCubeCodes(query_min, query_max);

        int bucket_id = hash_time_builder.locateFirstBucketForQuery(start_ts);
        const auto& buckets = hash_time_builder.buckets();
        while (bucket_id >= 0 && static_cast<std::size_t>(bucket_id) < buckets.size()) {
            const auto& bucket = buckets[static_cast<std::size_t>(bucket_id)];
            if (bucket.start_ts > end_ts) {
                break;
            }
            if (bucket.end_ts >= start_ts &&
                static_cast<std::size_t>(bucket_id) < hash_bucket_segment_trees.size()) {
                const auto [local_l, local_r] =
                    bucket.localRangeForQuery(start_ts, end_ts,
                                              hash_time_builder.config().time_slot_seconds);
                auto st = hash_bucket_segment_trees[static_cast<std::size_t>(bucket_id)];
                if (st != nullptr && local_l != -1) {
                    auto candidates = st->getCandidateIntervals(local_l, local_r, query_codes);
                    result.reserve(result.size() + candidates.size());
                    for (const auto& interval : candidates) {
                        result.emplace_back(interval.left, interval.right,
                                            bucket.bucket_id, interval.tokens);
                    }
                }
            }
            bucket_id = bucket.next_bucket_id;
        }
        return result;
    }

    std::string_view K_token, K_w, K_h, K_aes;
    int key_colnum, join_column, record_num;
    std::size_t limit_n;
    int query_runs;
    std::string condition;
    std::shared_ptr<HashSetup_JXTp> table_1;
    std::shared_ptr<HashSetup_JXTp> table_2;
    std::string table1_path;
    std::string table2_path;
    std::string_view join_attr1, join_attr2;
    std::string start_time, end_time;
    std::string keyword2_storage;
    std::string_view keyword2;
    double lat_min, lat_max, lon_min, lon_max;
    std::shared_ptr<Server_JXTp> server;
    AdaptiveTimeBucketConfig hash_time_config;
    AdaptiveTimeBucketBuilder hash_time_builder;
    std::vector<std::shared_ptr<SegmentTree>> hash_bucket_segment_trees;
    std::shared_ptr<CubeCode> hash_cube_code;
};

// 测试 Setup_JXTp::construct 是否正确生成 明文倒排索引
// TEST_F(JXTpTest, ConstructTset) {
//     ASSERT_FALSE(table_1->getTset().empty()) << "table_1 tset should not be empty";
//     ASSERT_FALSE(table_2->getTset().empty()) << "table_2 tset should not be empty";

//     // 验证 tset 中是否包含空间编码
//     bool found_spatial = false;
//     for (const auto& [token, value] : table_1->getTset()) {
//         std::string token_str = Setup_JXTp::toBase64(token);
//         if (token_str.find("spatial_code") != std::string::npos) {
//             found_spatial = true;
//             std::cout << "Found spatial token: " << token_str << std::endl;
//             break;
//         }
//     }
//     EXPECT_TRUE(found_spatial) << "tset should contain spatial_code tokens";
// }

// 测试 CubeCode 编码一致性,以下通过测试，符合需求
// TEST_F(JXTpTest, CubeCodeConsistency) {
//     std::vector<double> min_vals = {0,0};
//     std::vector<double> max_vals = {10,10};
//     CubeCode cube_code(2, min_vals, max_vals, 3);

//     // 测试点
//     std::vector<double> point = {3,7};
//     auto data_codes = cube_code.generateDataCubeCodes(point);
//     ASSERT_EQ(data_codes.size(), 3) << "Expected 2 data code for level 3";
   
//     std::cout << std::endl;
//     std::vector<std::string> expected_codes = {
//         "2.500000,7.500000,1",
//         "3.750000,6.250000,2",
//         "3.125000,6.875000,3"
//     };

//     std::cout << "Data code: ";
//     for (const auto& code : data_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;

//     // 判断每个编码是否和预期一致
//     for (size_t i = 0; i < expected_codes.size(); ++i) {
//         EXPECT_EQ(data_codes[i], expected_codes[i]) 
//             << "Mismatch at level " << (i + 1);
//     }


//     // 测试查询范围
//     std::vector<double> query_min = {2,2};
//     std::vector<double> query_max = {7,7};
//     auto query_codes = cube_code.generateQueryCubeCodes(query_min, query_max);
//     ASSERT_FALSE(query_codes.empty()) << "Query codes should not be empty";
//     std::cout << "Query codes: ";
//     for (const auto& code : query_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;

//     //query_codes=["3.75,3.75,2",
//                 //  "3.75,6.25,2", "6.25,3.75,2", "6.25,6.25,2"
//                 // "3.125,5.625,3", "3.125,6.875,3", "4.375,5.625,3", "4.375,6.875,3",
//                 // "5.625,3.125,3","5.625,4.375,3","5.625,5.625,3","5.625,6.875,3",
//                 // "6.875,3.125,3","6.875,4.375,3","6.875,5.625,3","6.875,6.875,3",
//                 // "1.875,1.875,3", "1.875,3.125,3", "1.875,4.375,3", "1.875,5.625,3","1.875,6.875,3",
//                 // "3.125,1.875,3","4.375,1.875,3","5.625,1.875,3","6.875,1.875,3"]
    
//     // 验证数据点编码是否在查询编码中
//     bool any_matched = false;
//     for (const auto& dc : data_codes) {
//         if (std::find(query_codes.begin(), query_codes.end(), dc) != query_codes.end()) {
//             any_matched = true;
//             break;
//         }
//     }
//     EXPECT_TRUE(any_matched) << "None of the data codes matched query codes";
// }

// TEST_F(JXTpTest, CubeCodeLatLonConsistency) {
//     std::vector<double> min_vals = {40.5, -74.2};
//     std::vector<double> max_vals = {41.0, -73.7};
//     CubeCode cube_code(2, min_vals, max_vals, 3);

//     // 测试点
//     std::vector<double> point = {40.65,-74.1};
//     auto data_codes = cube_code.generateDataCubeCodes(point);
//     ASSERT_EQ(data_codes.size(), 3) << "Expected 2 data code for level 3";
   

//     std::cout << "Data code: ";
//     for (const auto& code : data_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;


//     // 测试查询范围
//     std::vector<double> query_min = {40.65, -74.11};
//     std::vector<double> query_max = {40.66, -74.09};
//     auto query_codes = cube_code.generateQueryCubeCodes(query_min, query_max);
//     ASSERT_FALSE(query_codes.empty()) << "Query codes should not be empty";
//     std::cout << "Query codes: ";
//     for (const auto& code : query_codes) {
//         std::cout << code << " ";
//     }
//     std::cout << std::endl;

//     bool any_matched = false;
//     for (const auto& dc : data_codes) {
//         if (std::find(query_codes.begin(), query_codes.end(), dc) != query_codes.end()) {
//             any_matched = true;
//             break;
//         }
//     }
//     EXPECT_TRUE(any_matched) << "None of the data codes matched query codes";
// }

// 测试 BPlusTree::query_sql
// TEST_F(JXTpTest, BPlusTreeQuerySql) {
//     auto id_tokens1 = table_1->getCandidateIntervals(start_time, end_time, lat_min, lat_max, lon_min, lon_max);
//     EXPECT_FALSE(id_tokens1.empty()) << "id_tokens1 should not be empty";

//     // for (const auto& interval : id_tokens1) {
//     //     int start_hour = interval.left / 6;
//     //     int start_min = (interval.left % 6) * 10;
//     //     int end_hour = interval.right / 6;
//     //     int end_min = (interval.right % 6) * 10;
//     //     std::cout << "Time Range: "
//     //               << std::setw(2) << std::setfill('0') << start_hour << ":"
//     //               << std::setw(2) << std::setfill('0') << start_min << " - "
//     //               << std::setw(2) << std::setfill('0') << end_hour << ":"
//     //               << std::setw(2) << std::setfill('0') << end_min
//     //               << ", Tokens: " << interval.tokens.size() << std::endl;
//     // }
// }

// 测试 JXTp::run 和 Server_JXTp::search
TEST_F(JXTpTest, JXTpRunAndSearch) {
    if (GetEnvInt("JXT2_SETUP_ONLY", 0) != 0) {
        GTEST_SKIP() << "setup-only benchmark run";
    }
    std::cout << "------------ Hash JXT+ search ------------\n";
    double search_all = 0;
    int loop_count = query_runs; // 循环次数
    for(int x = 0; x < loop_count; ++x) {
        auto search_start = std::chrono::high_resolution_clock::now();
        auto join_hash1 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr1});
        auto join_hash2 = Hash::Get_SHA_256(std::string{K_h} + std::string{join_attr2});

        // 计算时间范围对应的时间戳和区间
        // struct tm tm_start = {}, tm_end = {};
        // strptime(start_time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm_start);
        // strptime(end_time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm_end);
        // time_t start_t = timegm(&tm_start);
        // time_t end_t = timegm(&tm_end);
        // // long long start_ts = start_t - (start_t % 86400); // 精确到天
        // // long long end_ts = end_t - (end_t % 86400) + 86399;
        // int start_interval = tm_start.tm_hour * 6 + tm_start.tm_min / 10; // 10 分钟区间
        // int end_interval = tm_end.tm_hour * 6 + tm_end.tm_min / 10;

        // 生成空间编码，针对每一个转换为token，查看是否有符合的，但是最后需要取并集，再和时间戳的取交集
        // TODO:是否存在一种方案，不需要取交集
        std::vector<double> query_min = {lat_min, lon_min};
        std::vector<double> query_max = {lat_max, lon_max};
        auto query_codes = table_1->getCubeCode()->generateQueryCubeCodes(query_min, query_max);
        // std::vector<std::string> keyword1_list;
        // for(const auto& code : query_codes) {
        //     // keyword1_list.push_back("spatial_code:" + code);
        //     keyword1_list.push_back(code);//都在第三层："40.718750,-73.981250,3"；"40.781250,-73.981250,3"
        // }
        

        //有关于范围查询映射为点查询
        // Step 3.1: Compute id tokens for range query
        // 后续使用 server 的 segment_tree1 和 segment_tree2
        // auto id_tokens1 = table_1.getCandidateIntervals(id_start, id_end,std::string{keyword1});

        //TODO：这里还需要返回符合的时间戳区间且包含经纬度的关键词，为keyword0服务
        std::vector<SegmentTree::IntervalResult> id_tokens1;
        // for(const auto& keyword1:keyword1_list) {
        //     auto tokens = table_1->getCandidateIntervals(start_time, end_time, lat_min, lat_max, lon_min, lon_max);
        //     id_tokens1.insert(id_tokens1.end(), tokens.begin(), tokens.end());
        // }
        id_tokens1 = table_1->getCandidateIntervals(start_time, end_time, lat_min, lat_max, lon_min, lon_max);
        if(x==0) std::cout << "id_tokens1.size() = " << id_tokens1.size() << std::endl;    //区间的size

        //为每个时间戳和区间生成join—token
        std::vector<std::vector<std::vector<unsigned char>>> join_token01(id_tokens1.size());
        std::vector<std::vector<std::vector<unsigned char>>> join_token02(id_tokens1.size());

        for (size_t i = 0; i < id_tokens1.size(); ++i) {
            const auto& interval = id_tokens1[i];
            int interval_idx = interval.left;
            //long long timestamp = start_ts + (interval_idx / 144) * 86400;
            //long long timestamp = Setup_JXTp::date_to_timestamp(start_time.substr(0, 10));
            const long long bucket_id = interval.day_ts;
            std::string keyword0 = RangeTimeKey(bucket_id, interval_idx);
            std::string stag_input0 = std::string{K_token} + keyword0 + std::string{join_attr1} + "1";
            auto stag0 = Hash::Get_SHA_256(stag_input0);

            int cnt0 = server->tset_table1_cnt(stag0);

            join_token01[i].resize(cnt0); //看里面tokens的数量
            join_token02[i].resize(cnt0);

            auto w0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword0} + "0");
            //TODO:去除table2的keyword，改为join-attr
            auto y0 = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword2} + "0");

            for (int j = 0; j < cnt0; ++j) {
                auto w_cnt0 = Hash::Get_SHA_256(std::string{K_w} + keyword0 + std::to_string(j + 1));
                join_token01[i][j] = tool::Xor(tool::Xor(w0, w_cnt0), join_hash1);
                join_token02[i][j] = tool::Xor(tool::Xor(y0, w_cnt0), join_hash2);
            }
        }

        // 解密并验证结果
        std::set<std::string> decrypted_range;
        std::set<std::string> decrypted_stag1;

        // --- 优化的核心：消除 for(auto keyword1 : ...) 循环调用 server->search ---
        std::vector<std::vector<unsigned char>> stags1_batch;
        std::vector<std::vector<std::vector<unsigned char>>> stokens_batch;
        std::vector<std::vector<std::vector<unsigned char>>> xtokens_batch;
        std::vector<std::vector<unsigned char>> k_decs1_batch; // 存储所有可能的解密密钥

        auto y = Hash::Get_SHA_256(std::string{K_w} + std::string{keyword2} + "0");

        // 1. 批处理准备：在一个循环内准备好所有 keyword1 相关的令牌和解密密钥
        // 生成 stag1 和 join_token1, join_token2
        for(auto keyword1 : query_codes) { //73.996875,4 "40.734375,-73.996875,4" "40.703125,-73.965625,4" "40.734375,-73.965625,4" "40.765625,-73.996875,4" "40.765625,-73.965625,4"
            //准备stag1
            std::string stag_input1 = std::string{K_token} + keyword1 + std::string{join_attr1} + "1";
            stags1_batch.push_back(Hash::Get_SHA_256(stag_input1));

            //准备stoken和xtoken
            int cnt1 = server->tset_table1_cnt(stags1_batch.back());
            if (cnt1 == 0) {
                // 如果这个keyword没有对应条目，可以跳过，或添加空vector占位
                stokens_batch.emplace_back();
                xtokens_batch.emplace_back();
                continue;
            }

            std::vector<std::vector<unsigned char>> join_token1(cnt1), join_token2(cnt1);
            auto w = Hash::Get_SHA_256(std::string{K_w} + keyword1 + "0");
            

            for (int i = 0; i < cnt1; ++i) {
                auto w_cnt = Hash::Get_SHA_256(std::string{K_w} + keyword1 + std::to_string(i + 1));
                join_token1[i] = tool::Xor(tool::Xor(w, w_cnt), join_hash1);
                join_token2[i] = tool::Xor(tool::Xor(y, w_cnt), join_hash2);
            }

            stokens_batch.push_back(join_token1);
            xtokens_batch.push_back(join_token2);

            //准备解密密钥
            k_decs1_batch.push_back(Hash::Get_SHA_256(std::string{K_aes} + keyword1));
            // 将 join_token01, join_token02, id_tokens1, join_token1, join_token2 和 stag1 传递给 server->search
            //TODO: 优化，在每次的循环中，只有 keyword1 变化，其他参数保持不变，相当于只有res_stag1 变化
            // auto [res_range, res_stag1] = server->search(join_token01, join_token02, id_tokens1, join_token1, join_token2, stag1);
            
        }
        auto k_dec2 = Hash::Get_SHA_256(std::string{K_aes} + std::string{keyword2});

        auto [res_range, res_stag1] = server->search(join_token01, join_token02, id_tokens1, stokens_batch, xtokens_batch, stags1_batch);

      

        //计算查询范围的密钥
        std::vector<std::vector<std::vector<unsigned char>>> k_dec01(id_tokens1.size());
        std::vector<std::vector<std::vector<unsigned char>>> k_dec02(id_tokens1.size());
        for (size_t i = 0; i < id_tokens1.size(); ++i) {
            const auto& interval = id_tokens1[i];
            int interval_idx = interval.left;
            // long long timestamp = start_ts + (interval_idx / 144) * 86400;
            const long long bucket_id = interval.day_ts;
            std::string keyword0 = RangeTimeKey(bucket_id, interval_idx);
            std::string k_dec_input01 = std::string{K_aes} + keyword0;
            std::string k_dec_input02 = std::string{K_aes} + std::string{keyword2};
            k_dec01[i].resize(res_range.size());
            k_dec02[i].resize(res_range.size());
            for (size_t j = 0; j < res_range.size(); ++j) {
                k_dec01[i][j] = Hash::Get_SHA_256(k_dec_input01);
                k_dec02[i][j] = Hash::Get_SHA_256(k_dec_input02);
            }
        }
            
        // if (x == 0) std::cout << "k_dec01 size: " << k_dec01.size() << ", k_dec02 size: " << k_dec02.size() << std::endl;

        for (size_t i = 0; i < res_range.size(); ++i) {
            int key_t = i/2;
            
            if (key_t >= id_tokens1.size() || key_t >= k_dec01.size() || key_t >= k_dec02.size()) {
                // std::cerr << "Error: key_t " << key_t << " out of bounds at i=" << i << std::endl;
                continue;
            }
            const auto& res_i = res_range[i];
            if (i % 2 == 0) {
                for (const auto& r : res_i) {
                    if (auto decrypted = AESUtil::decrypt(k_dec01[key_t][0], r);  decrypted) {
                        decrypted_range.insert(*decrypted);
                        // std::cout << *decrypted << ",\n";
                        // outfile << *decrypted << "," << '\n';  // table1 有值，table2 空
                    }
                }
            } else {
                for (const auto& r : res_i) {
                    if (auto decrypted = AESUtil::decrypt(k_dec02[key_t][0], r);  decrypted) {
                        decrypted_range.insert(*decrypted);
                        // std::cout << *decrypted << ",\n";
                        // outfile << "," << *decrypted << '\n';  // table1 空，table2 有值
                    }
                }
            }
        }

        // 解密 res_stag1，需要尝试所有可能的密钥
        for (size_t i = 0; i < res_stag1.size(); ++i) {
            const auto& res_i = res_stag1[i];
            if (i % 2 == 0) {
                for (const auto& r : res_i) {
                    for(const auto& k_dec1 : k_decs1_batch){ //尝试所有可能的密钥
                        if (auto decrypted = AESUtil::decrypt(k_dec1, r);  decrypted) {
                            decrypted_stag1.insert(*decrypted);
                            // std::cout << *decrypted << ",\n";
                            break;
                        }
                    }
                }
            } else {
                for (const auto& r : res_i) {
                    if (auto decrypted = AESUtil::decrypt(k_dec2, r);  decrypted) {
                        decrypted_stag1.insert(*decrypted);
                        // std::cout << *decrypted << ",\n";
                    }
                }
            }
        }
        

        if (x == 0) {
            const auto unified_expected = BuildUnifiedGroundTruth(
                table1_path, table2_path, limit_n,
                start_time, end_time,
                lat_min, lat_max, lon_min, lon_max,
                join_attr2, keyword2);
            PrintUnifiedGroundTruthStats(
                decrypted_range, decrypted_stag1, unified_expected);
            const std::string default_expected_path =
                std::string(DATA_DIR) + "/Spatio-Temporal-Datasets/filtered_ids_all.csv";
            idtoken_metrics::PrintFinalResultStats(
                decrypted_range, decrypted_stag1, default_expected_path);
        }

        // // outfile.close();
        // std::set<std::string> final_result;
        // std::set_intersection(
        //     decrypted_range.begin(), decrypted_range.end(),
        //     decrypted_stag1.begin(), decrypted_stag1.end(),
        //     std::inserter(final_result, final_result.begin())
        // );
            
        // // 读取预期结果:以下部分属于验证
        // std::set<std::string> expected_range;
        // // std::ifstream file("/nvme/baum/git-project/JXT2/data/Spatio-Temporal-Datasets/filtered_ids_200000.csv"); // 替换为实际路径
        // std::ifstream file("/nvme/baum/git-project/JXT2/data/Spatio-Temporal-Datasets/filtered_ids_all.csv");
        // if (!file.is_open()) {
        //     FAIL() << "Failed to open CSV file for expected results";
        // }
        // std::string line;
        // std::getline(file, line); // 跳过表头
        // while (std::getline(file, line)) {
        //     std::istringstream iss(line);
        //     std::string id1, id2;
        //     if (std::getline(iss, id1, ',') && std::getline(iss, id2, ',')) {
        //         expected_range.insert(id1);
        //         expected_range.insert(id2);
        //     }
        // }
        // file.close();

        // EXPECT_EQ(final_result, expected_range) << "Decrypted results should match expected results";
        auto search_end = std::chrono::high_resolution_clock::now();
        search_all+= std::chrono::duration<double, std::nano>(search_end - search_start).count();
        // if (x == 0) std::cout << "res_range size: " << res_range.size() << ", res_stag1 size: " << res_stag1.size() << "\n";
        // if (x == 0) std::cout << "final result size: " << final_result.size() << "\n";
    }
    std::cout << "Hash JXT+ average search time: " << search_all / (loop_count* 1e6) << " ms\n";

    // 读取预期结果:以下部分属于验证
    // std::set<std::string> expected_range;
    // std::ifstream file("/nvme/baum/git-project/JXT2/data/Spatio-Temporal-Datasets/filtered_ids_all.csv"); // 替换为实际路径
    // if (!file.is_open()) {
    //     FAIL() << "Failed to open CSV file for expected results";
    // }
    // std::string line;
    // std::getline(file, line); // 跳过表头
    // while (std::getline(file, line)) {
    //     std::istringstream iss(line);
    //     std::string id1, id2;
    //     if (std::getline(iss, id1, ',') && std::getline(iss, id2, ',')) {
    //         expected_range.insert(id1);
    //         expected_range.insert(id2);
    //     }
    // }
    // file.close();

    // EXPECT_EQ(final_result, expected_range) << "Decrypted results should match expected results";
}

// 测试 stag1 是否在 tset 中
// TEST_F(JXTpTest, Stag1InTset) {
//     std::vector<double> query_min = {lat_min, lon_min};
//     std::vector<double> query_max = {lat_max, lon_max};
//     auto query_codes = table_1->getBPlusTree()->getCubeCode()->generateQueryCubeCodes(query_min, query_max);
//     ASSERT_FALSE(query_codes.empty()) << "Query codes should not be empty";

//     // std::string keyword1 = "spatial_code:" + query_codes[0];
//     std::string keyword1 =  query_codes[0]; // 直接使用编码，不加前缀
//     std::string stag_input1 = std::string{K_token} + keyword1 + std::string{join_attr1} + "1";
//     auto stag1 = Hash::Get_SHA_256(stag_input1);
//     std::cout << "Generated stag1: " << Setup_JXTp::toBase64(stag1) << std::endl;

//     auto& tset = table_1->getTset();
//     auto it = tset.find(stag1);
//     EXPECT_NE(it, tset.end()) << "stag1 should be found in tset for keyword: " << keyword1;
// }

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
