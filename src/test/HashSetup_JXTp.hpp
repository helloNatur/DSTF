#pragma once

#include "Bloom.hpp"
#include "Setup_JXTp.hpp"
#include "SegmentTree.h"
#include "TimeUtil.h"
#include "adaptive_time_bucket.hpp"
#include "cube_code.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class HashSetup_JXTp {
private:
    inline static const std::string_view K_aes = "8975924566f6e252";
    inline static const std::string_view K_token = "89b7a92966f6eb32";
    inline static const std::string_view K_w = "7975922666f6eb02";
    inline static const std::string_view K_z = "9862192ad6f6ef65";
    inline static const std::string_view K_h = "9874a22554e7db85";

    using VectorHash = Setup_JXTp::VectorHash;

    int table_id;
    int key_column;
    int join_column;
    int record_num;
    std::size_t limit_n;
    std::string condition;
    bool build_range_index;

    std::vector<std::string> id;
    std::vector<std::vector<std::string>> join_attr;
    std::unordered_map<std::string, std::vector<int>> reverse_id;
    std::optional<Bloom> f;
    std::unordered_map<std::vector<unsigned char>,
                       std::vector<std::vector<unsigned char>>,
                       VectorHash> tset;
    std::unordered_map<long, std::vector<std::vector<unsigned char>>> cset;

    std::shared_ptr<CubeCode> cube_code;
    AdaptiveTimeBucketConfig time_config;
    AdaptiveTimeBucketBuilder time_builder;
    std::vector<std::shared_ptr<SegmentTree>> bucket_segment_trees;
    std::size_t indexed_points = 0;

public:
    HashSetup_JXTp(int table_id_,
                   int key_column_num,
                   int join_column_num,
                   int record,
                   std::string condition_t,
	                   std::size_t limit_n_,
	                   bool build_range_index_,
	                   std::size_t legacy_hash_index_capacity = 8,
	                   std::size_t hash_pool_bytes = 256ULL * 1024ULL * 1024ULL,
	                   std::vector<double> min_bounds = {},
	                   std::vector<double> max_bounds = {},
	                   int cube_code_level = 8);

    void construct();

    std::unordered_map<std::vector<unsigned char>,
                       std::vector<std::vector<unsigned char>>,
                       VectorHash>& getTset() {
        return tset;
    }

    const std::unordered_map<std::vector<unsigned char>,
                             std::vector<std::vector<unsigned char>>,
                             VectorHash>& getTset() const {
        return tset;
    }

    [[nodiscard]] Bloom getF() const { return *f; }

    [[nodiscard]] auto getCset() const
        -> std::unordered_map<long, std::vector<std::vector<unsigned char>>> {
        return cset;
    }

    [[nodiscard]] std::shared_ptr<CubeCode> getCubeCode() const {
        return cube_code;
    }

    [[nodiscard]] std::size_t getIndexedPointCount() const {
        return indexed_points;
    }

    [[nodiscard]] std::size_t getDayCount() const {
        return time_builder.buckets().size();
    }

    [[nodiscard]] std::size_t getRangeIndexStorageBytes() const;

    std::vector<SegmentTree::IntervalResult> getCandidateIntervals(
        const std::string& start_time,
        const std::string& end_time,
        double lat_min,
        double lat_max,
        double lon_min,
        double lon_max) const;
};
