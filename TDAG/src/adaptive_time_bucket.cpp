#include "adaptive_time_bucket.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {

constexpr long long kSecondsPerDay = 86400;
constexpr int kLocatorSlotsPerDay = 144;

int GetEnvPositiveInt(const char* name, int fallback) {
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

long long FloorDiv(long long value, long long divisor) {
    if (divisor <= 0) {
        return 0;
    }
    if (value >= 0) {
        return value / divisor;
    }
    return -((-value + divisor - 1) / divisor);
}

}  // namespace

DayLocatorEntry::DayLocatorEntry() {
    slot_to_bucket.fill(-1);
}

int AdaptiveTimeBucket::localSlotForAbsSlot(long long abs_slot) const {
    auto it = abs_slot_to_local_slot.find(abs_slot);
    if (it == abs_slot_to_local_slot.end()) {
        return -1;
    }
    return it->second;
}

std::pair<int, int> AdaptiveTimeBucket::localRangeForQuery(
    long long query_start_ts,
    long long query_end_ts,
    int time_slot_seconds) const {
    if (occupied_abs_slots.empty() || query_start_ts > query_end_ts) {
        return {-1, -1};
    }

    const long long intersect_start = std::max(query_start_ts, start_ts);
    const long long intersect_end = std::min(query_end_ts, end_ts);
    if (intersect_start > intersect_end) {
        return {-1, -1};
    }

    // Use floor on both bounds to preserve 10-minute slot inclusiveness.
    const long long start_slot = FloorDiv(intersect_start, time_slot_seconds);
    const long long end_slot = FloorDiv(intersect_end, time_slot_seconds);

    auto left = std::lower_bound(occupied_abs_slots.begin(),
                                 occupied_abs_slots.end(),
                                 start_slot);
    auto right = std::upper_bound(occupied_abs_slots.begin(),
                                  occupied_abs_slots.end(),
                                  end_slot);
    if (left == right) {
        return {-1, -1};
    }

    const int local_l = static_cast<int>(std::distance(occupied_abs_slots.begin(), left));
    const int local_r = static_cast<int>(std::distance(occupied_abs_slots.begin(), right)) - 1;
    return {local_l, local_r};
}

AdaptiveTimeBucketBuilder::AdaptiveTimeBucketBuilder()
    : config_(AdaptiveTimeBucketBuilder::configFromEnv()) {}

AdaptiveTimeBucketBuilder::AdaptiveTimeBucketBuilder(AdaptiveTimeBucketConfig config)
    : config_(config) {
    config_.time_slot_seconds = std::max(1, config_.time_slot_seconds);
    config_.target_records_per_bucket = std::max(1, config_.target_records_per_bucket);
    config_.max_records_per_bucket = std::max(config_.target_records_per_bucket,
                                              config_.max_records_per_bucket);
    config_.max_occupied_slots_per_bucket = std::max(1, config_.max_occupied_slots_per_bucket);
}

AdaptiveTimeBucketConfig AdaptiveTimeBucketBuilder::configFromEnv() {
    AdaptiveTimeBucketConfig config;
    config.time_slot_seconds = GetEnvPositiveInt("JXT2_TIME_SLOT_SECONDS", 600);
    config.target_records_per_bucket =
        GetEnvPositiveInt("JXT2_TARGET_RECORDS_PER_BUCKET", 2000);
    config.max_records_per_bucket =
        GetEnvPositiveInt("JXT2_MAX_RECORDS_PER_BUCKET", 3000);
    config.max_occupied_slots_per_bucket =
        GetEnvPositiveInt("JXT2_MAX_OCCUPIED_SLOTS_PER_BUCKET", 256);
    config.max_records_per_bucket = std::max(config.target_records_per_bucket,
                                             config.max_records_per_bucket);
    return config;
}

long long AdaptiveTimeBucketBuilder::absSlotForTimestamp(long long ts) const {
    return FloorDiv(ts, config_.time_slot_seconds);
}

long long AdaptiveTimeBucketBuilder::dayStart(long long ts) const {
    return FloorDiv(ts, kSecondsPerDay) * kSecondsPerDay;
}

int AdaptiveTimeBucketBuilder::slotInDay(long long ts) const {
    const long long day_start = dayStart(ts);
    const long long offset = std::max(0LL, ts - day_start);
    const int slot = static_cast<int>(offset / config_.time_slot_seconds);
    return std::min(std::max(slot, 0), kLocatorSlotsPerDay - 1);
}

void AdaptiveTimeBucketBuilder::build(std::vector<AdaptiveTimePointRef> points) {
    buckets_.clear();
    day_locator_.clear();
    record_to_bucket_.clear();
    forced_split_same_abs_slot_ = 0;

    if (points.empty()) {
        return;
    }

    std::stable_sort(points.begin(), points.end(),
                     [](const AdaptiveTimePointRef& a, const AdaptiveTimePointRef& b) {
                         if (a.timestamp != b.timestamp) {
                             return a.timestamp < b.timestamp;
                         }
                         return a.record_id < b.record_id;
                     });

    std::vector<AdaptiveTimePointRef> current_points;
    std::vector<long long> current_slots;
    current_points.reserve(static_cast<std::size_t>(config_.max_records_per_bucket) + 1);
    current_slots.reserve(static_cast<std::size_t>(config_.max_occupied_slots_per_bucket) + 1);
    std::unordered_set<long long> current_slot_set;
    current_slot_set.reserve(static_cast<std::size_t>(config_.max_occupied_slots_per_bucket) * 2 + 1);

    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];
        const long long abs_slot = absSlotForTimestamp(point.timestamp);
        current_points.push_back(point);
        if (current_slot_set.insert(abs_slot).second) {
            current_slots.push_back(abs_slot);
        }

        const bool last_point = (i + 1 == points.size());
        const long long next_abs_slot =
            last_point ? std::numeric_limits<long long>::max()
                       : absSlotForTimestamp(points[i + 1].timestamp);
        const bool next_slot_differs = last_point || next_abs_slot != abs_slot;

        bool close_bucket = false;
        if (current_points.size() >= static_cast<std::size_t>(config_.max_records_per_bucket)) {
            close_bucket = true;
            if (!next_slot_differs) {
                ++forced_split_same_abs_slot_;
            }
        } else if (current_slot_set.size() >=
                       static_cast<std::size_t>(config_.max_occupied_slots_per_bucket) &&
                   next_slot_differs) {
            close_bucket = true;
        } else if (current_points.size() >=
                       static_cast<std::size_t>(config_.target_records_per_bucket) &&
                   next_slot_differs) {
            close_bucket = true;
        }

        if (close_bucket || last_point) {
            finalizeCurrentBucket(current_points, current_slots);
            current_points.clear();
            current_slots.clear();
            current_slot_set.clear();
        }
    }

    for (std::size_t i = 0; i < buckets_.size(); ++i) {
        buckets_[i].next_bucket_id =
            (i + 1 < buckets_.size()) ? static_cast<int>(i + 1) : -1;
    }
    buildDayLocator();
}

void AdaptiveTimeBucketBuilder::finalizeCurrentBucket(
    const std::vector<AdaptiveTimePointRef>& current_points,
    const std::vector<long long>& current_slots) {
    if (current_points.empty()) {
        return;
    }

    AdaptiveTimeBucket bucket;
    bucket.bucket_id = static_cast<int>(buckets_.size());
    bucket.start_ts = current_points.front().timestamp;
    bucket.end_ts = current_points.back().timestamp;
    bucket.record_count = static_cast<int>(current_points.size());
    bucket.occupied_abs_slots = current_slots;
    std::sort(bucket.occupied_abs_slots.begin(), bucket.occupied_abs_slots.end());
    bucket.occupied_abs_slots.erase(
        std::unique(bucket.occupied_abs_slots.begin(), bucket.occupied_abs_slots.end()),
        bucket.occupied_abs_slots.end());

    if (!bucket.occupied_abs_slots.empty()) {
        bucket.start_abs_slot = bucket.occupied_abs_slots.front();
        bucket.end_abs_slot = bucket.occupied_abs_slots.back();
    }
    bucket.abs_slot_to_local_slot.reserve(bucket.occupied_abs_slots.size() * 2 + 1);
    for (std::size_t i = 0; i < bucket.occupied_abs_slots.size(); ++i) {
        bucket.abs_slot_to_local_slot.emplace(bucket.occupied_abs_slots[i],
                                              static_cast<int>(i));
    }

    for (const auto& point : current_points) {
        record_to_bucket_[point.record_id] = bucket.bucket_id;
    }
    buckets_.push_back(std::move(bucket));
}

void AdaptiveTimeBucketBuilder::buildDayLocator() {
    if (buckets_.empty()) {
        return;
    }

    const long long first_day = dayStart(buckets_.front().start_ts);
    const long long last_day = dayStart(buckets_.back().end_ts);
    for (long long day = first_day; day <= last_day; day += kSecondsPerDay) {
        DayLocatorEntry entry;
        for (int slot = 0; slot < kLocatorSlotsPerDay; ++slot) {
            const long long slot_start = day + static_cast<long long>(slot) * config_.time_slot_seconds;
            entry.slot_to_bucket[slot] = firstBucketWithEndAtLeast(slot_start);
        }
        entry.first_bucket_id = entry.slot_to_bucket[0];
        day_locator_.emplace(day, entry);
    }
}

int AdaptiveTimeBucketBuilder::firstBucketWithEndAtLeast(long long ts) const {
    auto it = std::lower_bound(
        buckets_.begin(), buckets_.end(), ts,
        [](const AdaptiveTimeBucket& bucket, long long value) {
            return bucket.end_ts < value;
        });
    if (it == buckets_.end()) {
        return -1;
    }
    return it->bucket_id;
}

int AdaptiveTimeBucketBuilder::bucketIdForRecord(int record_id) const {
    auto it = record_to_bucket_.find(record_id);
    if (it == record_to_bucket_.end()) {
        return -1;
    }
    return it->second;
}

int AdaptiveTimeBucketBuilder::locateBucketForTimestamp(long long ts) const {
    const int bucket_id = firstBucketWithEndAtLeast(ts);
    if (bucket_id < 0) {
        return -1;
    }
    const auto& bucket = buckets_[static_cast<std::size_t>(bucket_id)];
    if (bucket.start_ts <= ts && ts <= bucket.end_ts) {
        return bucket_id;
    }
    return -1;
}

int AdaptiveTimeBucketBuilder::locateFirstBucketForQuery(long long query_start_ts) const {
    if (buckets_.empty()) {
        return -1;
    }
    if (query_start_ts <= buckets_.front().end_ts) {
        return buckets_.front().bucket_id;
    }
    if (query_start_ts > buckets_.back().end_ts) {
        return -1;
    }

    const long long day = dayStart(query_start_ts);
    const int slot = slotInDay(query_start_ts);
    auto it = day_locator_.find(day);
    if (it != day_locator_.end()) {
        const int bucket_id = it->second.slot_to_bucket[static_cast<std::size_t>(slot)];
        if (bucket_id >= 0) {
            return bucket_id;
        }
    }
    return firstBucketWithEndAtLeast(query_start_ts);
}

std::size_t AdaptiveTimeBucketBuilder::metadataSizeBytes() const {
    std::size_t bytes = sizeof(*this);
    bytes += buckets_.capacity() * sizeof(AdaptiveTimeBucket);
    bytes += record_to_bucket_.size() * (sizeof(int) * 2 + sizeof(void*));
    bytes += day_locator_.size() * (sizeof(long long) + sizeof(DayLocatorEntry) + sizeof(void*));
    for (const auto& bucket : buckets_) {
        bytes += bucket.occupied_abs_slots.capacity() * sizeof(long long);
        bytes += bucket.abs_slot_to_local_slot.size() *
                 (sizeof(long long) + sizeof(int) + sizeof(void*));
    }
    return bytes;
}

void AdaptiveTimeBucketBuilder::printStats(std::ostream& os) const {
    std::size_t total_records = 0;
    std::size_t max_records = 0;
    std::size_t total_slots = 0;
    std::size_t max_slots = 0;
    for (const auto& bucket : buckets_) {
        total_records += static_cast<std::size_t>(bucket.record_count);
        max_records = std::max(max_records, static_cast<std::size_t>(bucket.record_count));
        total_slots += bucket.occupied_abs_slots.size();
        max_slots = std::max(max_slots, bucket.occupied_abs_slots.size());
    }

    const double avg_records = buckets_.empty()
        ? 0.0
        : static_cast<double>(total_records) / static_cast<double>(buckets_.size());
    const double avg_slots = buckets_.empty()
        ? 0.0
        : static_cast<double>(total_slots) / static_cast<double>(buckets_.size());

    os << "[AdaptiveBucket] time_slot_seconds=" << config_.time_slot_seconds << "\n";
    os << "[AdaptiveBucket] target_records_per_bucket="
       << config_.target_records_per_bucket << "\n";
    os << "[AdaptiveBucket] max_records_per_bucket="
       << config_.max_records_per_bucket << "\n";
    os << "[AdaptiveBucket] max_occupied_slots_per_bucket="
       << config_.max_occupied_slots_per_bucket << "\n";
    os << "[AdaptiveBucket] bucket_count=" << buckets_.size() << "\n";
    os << std::fixed << std::setprecision(2);
    os << "[AdaptiveBucket] avg_records_per_bucket=" << avg_records << "\n";
    os << "[AdaptiveBucket] max_records_per_bucket_observed=" << max_records << "\n";
    os << "[AdaptiveBucket] avg_occupied_slots=" << avg_slots << "\n";
    os << "[AdaptiveBucket] max_occupied_slots_observed=" << max_slots << "\n";
    os << "[AdaptiveBucket] forced_split_same_abs_slot="
       << forced_split_same_abs_slot_ << "\n";
    os << "[DayLocator] day_count=" << day_locator_.size() << "\n";
}
