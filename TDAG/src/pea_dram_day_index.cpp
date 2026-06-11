#include "pea_dram_day_index.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

#include "PeaHash/pea_hash.h"

class PeaDramDayIndex::Impl {
public:
    Impl(std::size_t init_capacity,
         std::uint64_t thread_num,
         std::size_t pool_size_bytes)
        : hash_(nullptr) {
        const std::string pool_name = "/tmp/jxt2_pea_dram_day_index.data";
        SpaceManager::Initialize(pool_name.c_str(), pool_size_bytes, thread_num);
        hash_ = std::make_unique<pea::PeaHashing<std::uint64_t>>(init_capacity, thread_num);
    }

    ~Impl() {
        hash_.reset();
        if (SpaceManager::Get() != nullptr) {
            SpaceManager::Get()->Close_pool();
        }
    }

    bool insert(long long day_ts, void* root) {
        const std::uint64_t key = normalizeKey(day_ts);
        auto [it, inserted] = buckets_.emplace(
            day_ts, std::make_unique<DayBucket>(DayBucket{day_ts, root, nullptr}));

        if (!inserted) {
            it->second->root = root;
            return false;
        }

        auto next_it = std::next(it);
        it->second->next = (next_it == buckets_.end()) ? nullptr : next_it->second.get();
        if (it != buckets_.begin()) {
            auto prev_it = std::prev(it);
            prev_it->second->next = it->second.get();
        }

        Value_t value = reinterpret_cast<Value_t>(it->second.get());
        const int rc = hash_->Insert(key, value);
        if (rc != 0) {
            throw std::runtime_error("PeaHash insert failed for day " + std::to_string(day_ts));
        }
        return true;
    }

    DayBucket* getBucket(long long day_ts) const {
        Value_t value = hash_->Get(normalizeKey(day_ts));
        if (value == NONE) {
            return nullptr;
        }
        return reinterpret_cast<DayBucket*>(const_cast<char*>(value));
    }

    DayBucket* firstIntersectingDay(long long start_day_ts) const {
        if (auto* exact = getBucket(start_day_ts)) {
            return exact;
        }

        auto it = buckets_.lower_bound(start_day_ts);
        if (it == buckets_.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    std::vector<DayBucket*> intersectingDays(long long start_day_ts,
                                             long long end_day_ts) const {
        std::vector<DayBucket*> days;
        DayBucket* day = firstIntersectingDay(start_day_ts);
        while (day != nullptr && day->day_ts <= end_day_ts) {
            days.push_back(day);
            day = day->next;
        }
        return days;
    }

    std::size_t size() const {
        return buckets_.size();
    }

private:
    static std::uint64_t normalizeKey(long long day_ts) {
        return static_cast<std::uint64_t>(day_ts);
    }

    std::unique_ptr<pea::PeaHashing<std::uint64_t>> hash_;
    std::map<long long, std::unique_ptr<DayBucket>> buckets_;
};

PeaDramDayIndex::PeaDramDayIndex(std::size_t init_capacity,
                                 std::uint64_t thread_num,
                                 std::size_t pool_size_bytes)
    : impl_(std::make_unique<Impl>(init_capacity, thread_num, pool_size_bytes)) {}

PeaDramDayIndex::~PeaDramDayIndex() = default;

bool PeaDramDayIndex::insert(long long day_ts, void* root) {
    return impl_->insert(day_ts, root);
}

void* PeaDramDayIndex::get(long long day_ts) const {
    DayBucket* bucket = getBucket(day_ts);
    return bucket == nullptr ? nullptr : bucket->root;
}

PeaDramDayIndex::DayBucket* PeaDramDayIndex::getBucket(long long day_ts) const {
    return impl_->getBucket(day_ts);
}

PeaDramDayIndex::DayBucket* PeaDramDayIndex::firstIntersectingDay(long long start_day_ts) const {
    return impl_->firstIntersectingDay(start_day_ts);
}

std::vector<PeaDramDayIndex::DayBucket*> PeaDramDayIndex::intersectingDays(
    long long start_day_ts,
    long long end_day_ts) const {
    return impl_->intersectingDays(start_day_ts, end_day_ts);
}

std::size_t PeaDramDayIndex::size() const {
    return impl_->size();
}
