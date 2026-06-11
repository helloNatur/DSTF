#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

class PeaDramDayIndex {
public:
    struct DayBucket {
        long long day_ts;
        void* root;
        DayBucket* next;
    };

    explicit PeaDramDayIndex(std::size_t init_capacity = 8,
                             std::uint64_t thread_num = 1,
                             std::size_t pool_size_bytes = 256ULL * 1024ULL * 1024ULL);
    ~PeaDramDayIndex();

    PeaDramDayIndex(const PeaDramDayIndex&) = delete;
    PeaDramDayIndex& operator=(const PeaDramDayIndex&) = delete;

    bool insert(long long day_ts, void* root);
    void* get(long long day_ts) const;
    DayBucket* getBucket(long long day_ts) const;

    DayBucket* firstIntersectingDay(long long start_day_ts) const;
    std::vector<DayBucket*> intersectingDays(long long start_day_ts,
                                             long long end_day_ts) const;

    std::size_t size() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
