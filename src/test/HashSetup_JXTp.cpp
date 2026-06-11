#include "HashSetup_JXTp.hpp"

#include "AESUtil.hpp"
#include "Hash.hpp"
#include "tool.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {

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

std::string RangeTimeKey(int bucket_id, int local_slot) {
    return "bucket_" + std::to_string(bucket_id) + "_slot_" +
           std::to_string(local_slot);
}

struct RangeRecord {
    int record_id;
    long long timestamp;
    std::vector<std::string> codes;
};

}  // namespace

HashSetup_JXTp::HashSetup_JXTp(int table_id_,
                               int key_column_num,
                               int join_column_num,
                               int record,
                               std::string condition_t,
	                               std::size_t limit_n_,
	                               bool build_range_index_,
	                               std::size_t legacy_hash_index_capacity,
	                               std::size_t hash_pool_bytes,
	                               std::vector<double> min_bounds,
	                               std::vector<double> max_bounds,
	                               int cube_code_level)
		    : table_id{table_id_},
		      key_column{key_column_num},
		      join_column{join_column_num},
		      record_num{record},
		      limit_n{limit_n_},
		      condition{std::move(condition_t)},
		      build_range_index{build_range_index_} {
            (void)legacy_hash_index_capacity;
            (void)hash_pool_bytes;
		    if (min_bounds.size() != 2 || max_bounds.size() != 2) {
		        throw std::runtime_error("HashSetup_JXTp requires data-driven 2D bounds");
		    }
		    cube_code = std::make_shared<CubeCode>(
		        2, min_bounds, max_bounds, cube_code_level);
		}

void HashSetup_JXTp::construct() {
    id.resize(record_num + 1);
    join_attr.resize(record_num + 1, std::vector<std::string>(join_column));

    std::filesystem::path path = std::string(DATA_DIR) + "/table" +
        std::to_string(table_id) + "/table" + std::to_string(table_id) +
        "_k" + std::to_string(key_column) + "_j" + std::to_string(join_column) +
        "_" + std::to_string(record_num) + condition + ".csv";

    std::ifstream file{path};
    if (!file) {
        throw std::runtime_error("Failed to open " + path.string());
    }

    std::string line;
    int counter = 0;
    std::vector<RangeRecord> range_records;
    if (build_range_index) {
        range_records.reserve(limit_n);
    }
    while (std::getline(file, line)) {
        if (counter >= record_num + 1) {
            throw std::runtime_error("Too many records in CSV file");
        }
        if (counter > static_cast<int>(limit_n)) {
            break;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        auto record = SplitCsvLine(line);
        if (record.size() <= 8) {
            ++counter;
            continue;
        }

        id[counter] = record[8];
        for (int j = 0; j < join_column; ++j) {
            join_attr[counter][j] = record[0];
            if (counter != 0) {
                std::string kword = join_attr[0][j] + record[0];
                reverse_id[kword].push_back(counter);
            }
        }

        if (counter != 0) {
            const std::string& time_str = record[7];
            const long long timestamp = TimeUtil::to_timestamp(time_str);
            std::vector<double> point = {std::stod(record[4]), std::stod(record[5])};

            auto codes = DeduplicateCodesPreserveOrder(cube_code->generateDataCubeCodes(point));
            for (const auto& code : codes) {
                reverse_id[code].push_back(counter);
            }

            if (build_range_index) {
                range_records.push_back({counter, timestamp, std::move(codes)});
            }
        }
        ++counter;
    }

    if (build_range_index) {
        std::vector<AdaptiveTimePointRef> time_points;
        time_points.reserve(range_records.size());
        for (const auto& record : range_records) {
            time_points.push_back({record.record_id, record.timestamp});
        }

        time_config = AdaptiveTimeBucketBuilder::configFromEnv();
        time_builder = AdaptiveTimeBucketBuilder(time_config);
        time_builder.build(std::move(time_points));
        time_builder.printStats(std::cout);

        bucket_segment_trees.clear();
        bucket_segment_trees.resize(time_builder.buckets().size());
        for (const auto& bucket : time_builder.buckets()) {
            const int tree_size = std::max<int>(
                1, static_cast<int>(bucket.occupied_abs_slots.size()));
            bucket_segment_trees[static_cast<std::size_t>(bucket.bucket_id)] =
                std::make_shared<SegmentTree>(tree_size, 0.001, 442);
        }

        std::cout << "[HashSetup_JXTp] Building AdaptiveBucket / SegmentTree / BF index...\n";
        for (const auto& record : range_records) {
            const int bucket_id = time_builder.bucketIdForRecord(record.record_id);
            if (bucket_id < 0 ||
                static_cast<std::size_t>(bucket_id) >= time_builder.buckets().size()) {
                throw std::runtime_error("Adaptive bucket lookup failed for record_id " +
                                         std::to_string(record.record_id));
            }
            const auto& bucket = time_builder.buckets()[static_cast<std::size_t>(bucket_id)];
            const long long abs_slot = time_builder.absSlotForTimestamp(record.timestamp);
            const int local_slot = bucket.localSlotForAbsSlot(abs_slot);
            if (local_slot < 0) {
                throw std::runtime_error("Adaptive local slot lookup failed for record_id " +
                                         std::to_string(record.record_id));
            }

            const std::string time_key = RangeTimeKey(bucket_id, local_slot);
            reverse_id[time_key].push_back(record.record_id);

            const std::string token_input =
                std::string{K_token} + time_key + join_attr[0][0] + std::to_string(table_id);
            auto token = std::make_shared<std::vector<unsigned char>>(
                Hash::Get_SHA_256(token_input));
            bucket_segment_trees[static_cast<std::size_t>(bucket_id)]->update_deferred(
                local_slot, token, record.codes);
            ++indexed_points;
        }

        std::cout << "[HashSetup_JXTp] Finalizing deferred SegmentTree Bloom Filters...\n";
        auto finalize_start = std::chrono::high_resolution_clock::now();
        for (auto& segment_tree : bucket_segment_trees) {
            if (segment_tree) {
                segment_tree->finalize_bloom_filters();
            }
        }
        auto finalize_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> finalize_time =
            finalize_end - finalize_start;
        std::cout << "[HashSetup_JXTp] SegmentTree finalize complete: "
                  << finalize_time.count() << " ms\n";
        std::cout << "[AdaptiveBucket] Active Buckets: "
                  << time_builder.buckets().size() << "\n";
    }

    std::vector<std::vector<unsigned char>> join_hash(join_column);
    for (int j = 0; j < join_column; ++j) {
        join_hash[j] = Hash::Get_SHA_256(std::string{K_h} + join_attr[0][j]);
    }

    std::size_t total_xy_size = 0;
    for (const auto& [kword, reverse_tmp] : reverse_id) {
        total_xy_size += reverse_tmp.size() * join_column;
    }
    std::vector<long> xy(total_xy_size);
    int xy_counter = 0;

    for (const auto& [kword, reverse_tmp] : reverse_id) {
        if (reverse_tmp.empty()) {
            std::cerr << "Warning: empty reverse_tmp for keyword: " << kword << std::endl;
            continue;
        }

        auto w = Hash::Get_SHA_256(std::string{K_w} + kword + "0");
        auto K_enc = Hash::Get_SHA_256(std::string{K_aes} + kword);
        std::vector<std::vector<std::vector<unsigned char>>> t(join_column);

        for (std::size_t i = 0; i < reverse_tmp.size(); ++i) {
            int record_id = reverse_tmp[i];
            if (record_id >= static_cast<int>(id.size()) || id[record_id].empty()) {
                throw std::runtime_error("Invalid record_id or empty id: " +
                                         std::to_string(record_id));
            }

            auto w_cnt = Hash::Get_SHA_256(std::string{K_w} + kword + std::to_string(i + 1));
            auto ct_tmp = AESUtil::encrypt(K_enc, id[record_id]);
            if (ct_tmp.empty()) {
                throw std::runtime_error("AES encryption failed for record_id: " +
                                         std::to_string(record_id));
            }

            for (int j = 0; j < join_column; ++j) {
                if (xy_counter >= static_cast<int>(xy.size())) {
                    throw std::runtime_error("xy_counter out of bounds: " +
                                             std::to_string(xy_counter));
                }

                auto y = Hash::Get_SHA_256(std::string{K_z} + join_attr[record_id][j]);
                auto tset_each = tool::Xor(w_cnt, y);
                auto xor_result = tool::Xor(w, y);
                auto final_xor = tool::Xor(xor_result, join_hash[j]);
                if (final_xor.empty()) {
                    throw std::runtime_error("tool::Xor returned empty vector");
                }

                xy[xy_counter] = tool::bytesToLong(final_xor);
                if (auto [it, inserted] =
                        cset.emplace(xy[xy_counter],
                                     std::vector<std::vector<unsigned char>>{}); !inserted) {
                    it->second.push_back(ct_tmp);
                    tset_each = tool::Xor(tset_each,
                                          std::vector<unsigned char>{K_z.begin(), K_z.end()});
                } else {
                    it->second.push_back(ct_tmp);
                }
                if (i == 0) t[j] = {};
                t[j].push_back(tset_each);
                ++xy_counter;
            }
        }

        for (int i = 0; i < join_column; ++i) {
            auto token = Hash::Get_SHA_256(std::string{K_token} + kword +
                                           join_attr[0][i] + std::to_string(table_id));
            tset[token] = std::move(t[i]);
        }
    }

    f = Bloom::construct(xy, 64);
}

std::vector<SegmentTree::IntervalResult> HashSetup_JXTp::getCandidateIntervals(
    const std::string& start_time,
    const std::string& end_time,
    double lat_min,
    double lat_max,
    double lon_min,
    double lon_max) const {
    if (!build_range_index || bucket_segment_trees.empty()) {
        throw std::runtime_error("HashSetup_JXTp range index is disabled for this table");
    }

    std::vector<SegmentTree::IntervalResult> result;
    const long long start_ts = TimeUtil::to_timestamp(start_time);
    const long long end_ts = TimeUtil::to_timestamp(end_time);

    std::vector<double> query_min = {lat_min, lon_min};
    std::vector<double> query_max = {lat_max, lon_max};
    auto query_codes = cube_code->generateQueryCubeCodes(query_min, query_max);

    int bucket_id = time_builder.locateFirstBucketForQuery(start_ts);
    const auto& buckets = time_builder.buckets();
    while (bucket_id >= 0 && static_cast<std::size_t>(bucket_id) < buckets.size()) {
        const auto& bucket = buckets[static_cast<std::size_t>(bucket_id)];
        if (bucket.start_ts > end_ts) {
            break;
        }
        if (bucket.end_ts >= start_ts &&
            static_cast<std::size_t>(bucket_id) < bucket_segment_trees.size()) {
            const auto [local_l, local_r] =
                bucket.localRangeForQuery(start_ts, end_ts,
                                          time_builder.config().time_slot_seconds);
            auto st = bucket_segment_trees[static_cast<std::size_t>(bucket_id)];
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

std::size_t HashSetup_JXTp::getRangeIndexStorageBytes() const {
    if (!build_range_index || bucket_segment_trees.empty()) {
        return 0;
    }

    std::size_t bytes = time_builder.metadataSizeBytes();
    bytes += bucket_segment_trees.capacity() * sizeof(std::shared_ptr<SegmentTree>);
    for (const auto& segment_tree : bucket_segment_trees) {
        if (!segment_tree) {
            continue;
        }
        bytes += sizeof(SegmentTree);
        bytes += segment_tree->get_tree().capacity() * 128ULL;
    }
    return bytes;
}
