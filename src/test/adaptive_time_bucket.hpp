#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <unordered_map>
#include <utility>
#include <vector>

struct AdaptiveTimePointRef {
    int record_id = -1;
    long long timestamp = 0;
};

struct AdaptiveTimeBucketConfig {
    int time_slot_seconds = 600;
    int target_records_per_bucket = 2000;
    int max_records_per_bucket = 3000;
    int max_occupied_slots_per_bucket = 256;
};

struct AdaptiveTimeBucket {
    int bucket_id = -1;
    long long start_ts = 0;
    long long end_ts = 0;
    long long start_abs_slot = 0;
    long long end_abs_slot = 0;
    int record_count = 0;
    int next_bucket_id = -1;

    std::vector<long long> occupied_abs_slots;
    std::unordered_map<long long, int> abs_slot_to_local_slot;

    int localSlotForAbsSlot(long long abs_slot) const;
    std::pair<int, int> localRangeForQuery(long long query_start_ts,
                                           long long query_end_ts,
                                           int time_slot_seconds) const;
};

struct DayLocatorEntry {
    int first_bucket_id = -1;
    std::array<int, 144> slot_to_bucket;

    DayLocatorEntry();
};

class AdaptiveTimeBucketBuilder {
public:
    AdaptiveTimeBucketBuilder();
    explicit AdaptiveTimeBucketBuilder(AdaptiveTimeBucketConfig config);

    void build(std::vector<AdaptiveTimePointRef> points);

    const AdaptiveTimeBucketConfig& config() const { return config_; }
    const std::vector<AdaptiveTimeBucket>& buckets() const { return buckets_; }
    const std::unordered_map<long long, DayLocatorEntry>& dayLocator() const {
        return day_locator_;
    }

    int bucketIdForRecord(int record_id) const;
    int locateBucketForTimestamp(long long ts) const;
    int locateFirstBucketForQuery(long long query_start_ts) const;

    long long absSlotForTimestamp(long long ts) const;
    long long dayStart(long long ts) const;
    int slotInDay(long long ts) const;

    std::size_t metadataSizeBytes() const;
    std::size_t forcedSplitSameAbsSlotCount() const { return forced_split_same_abs_slot_; }
    void printStats(std::ostream& os) const;

    static AdaptiveTimeBucketConfig configFromEnv();

private:
    void finalizeCurrentBucket(const std::vector<AdaptiveTimePointRef>& current_points,
                               const std::vector<long long>& current_slots);
    void buildDayLocator();
    int firstBucketWithEndAtLeast(long long ts) const;

    AdaptiveTimeBucketConfig config_;
    std::vector<AdaptiveTimeBucket> buckets_;
    std::unordered_map<long long, DayLocatorEntry> day_locator_;
    std::unordered_map<int, int> record_to_bucket_;
    std::size_t forced_split_same_abs_slot_ = 0;
};
