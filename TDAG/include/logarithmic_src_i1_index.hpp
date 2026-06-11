#pragma once

#include "emm_interface.hpp"
#include "tdag.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class LogarithmicSrcI1Index {
public:
    struct Entry {
        int attr_value;
        int record_id;
    };

    struct RangeMeta {
        int attr_value;
        int start_pos;
        int end_pos;
    };

    LogarithmicSrcI1Index() = default;

    void build(const std::vector<Entry>& entries,
               KeywordMap& keyword_map,
               const std::string& label_prefix) {
        prefix_ = label_prefix;
        range_table_.clear();
        entry_count_ = entries.size();

        if (entries.empty()) {
            attr_tree_.reset();
            pos_tree_.reset();
            attr_full_range_ = {0, 0};
            pos_full_range_ = {0, 0};
            return;
        }

        std::vector<Entry> sorted = entries;
        std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
            if (a.attr_value != b.attr_value) {
                return a.attr_value < b.attr_value;
            }
            return a.record_id < b.record_id;
        });

        int max_attr = 0;
        for (const auto& entry : sorted) {
            if (entry.attr_value < 0) {
                throw std::invalid_argument("LogarithmicSrcI1Index attr_value must be non-negative");
            }
            max_attr = std::max(max_attr, entry.attr_value);
        }

        attr_tree_ = Tdag::initialize(heightForDomain(max_attr + 1));
        pos_tree_ = Tdag::initialize(heightForDomain(static_cast<int>(sorted.size())));
        attr_full_range_ = fullRangeForTree(max_attr + 1);
        pos_full_range_ = fullRangeForTree(static_cast<int>(sorted.size()));

        std::size_t group_begin = 0;
        while (group_begin < sorted.size()) {
            std::size_t group_end = group_begin + 1;
            while (group_end < sorted.size() &&
                   sorted[group_end].attr_value == sorted[group_begin].attr_value) {
                ++group_end;
            }

            const int range_id = static_cast<int>(range_table_.size()) + 1;
            range_table_.push_back(RangeMeta{
                sorted[group_begin].attr_value,
                static_cast<int>(group_begin),
                static_cast<int>(group_end - 1)
            });

            auto attr_paths = attr_tree_->descend_tree(sorted[group_begin].attr_value, attr_full_range_);
            for (const auto& attr_range : attr_paths) {
                keyword_map[i1Label(attr_range)].push_back(range_id);
            }

            for (std::size_t pos = group_begin; pos < group_end; ++pos) {
                auto pos_paths = pos_tree_->descend_tree(static_cast<int>(pos), pos_full_range_);
                for (const auto& pos_range : pos_paths) {
                    keyword_map[i2Label(pos_range)].push_back(sorted[pos].record_id);
                }
            }

            group_begin = group_end;
        }
    }

    std::vector<Label> getI1QueryLabels(int attr_start, int attr_end) const {
        if (!attr_tree_ || attr_start > attr_end) {
            return {};
        }
        attr_start = std::max(attr_start, attr_full_range_.first);
        attr_end = std::min(attr_end, attr_full_range_.second);
        if (attr_start > attr_end) {
            return {};
        }
        return {i1Label(attr_tree_->get_single_range_cover({attr_start, attr_end}))};
    }

    std::vector<std::pair<int, int>> filterAndMergePositionRanges(
        const std::vector<int>& decrypted_range_ids,
        int attr_start,
        int attr_end) const {
        std::vector<std::pair<int, int>> ranges;
        std::set<int> seen;

        for (int range_id : decrypted_range_ids) {
            if (range_id <= 0 || range_id > static_cast<int>(range_table_.size())) {
                continue;
            }
            if (!seen.insert(range_id).second) {
                continue;
            }
            const auto& meta = range_table_[static_cast<std::size_t>(range_id - 1)];
            if (meta.attr_value < attr_start || meta.attr_value > attr_end) {
                continue;
            }
            ranges.push_back({meta.start_pos, meta.end_pos});
        }

        if (ranges.empty()) {
            return ranges;
        }
        std::sort(ranges.begin(), ranges.end());

        std::vector<std::pair<int, int>> merged;
        merged.push_back(ranges.front());
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            auto& last = merged.back();
            if (ranges[i].first <= last.second + 1) {
                last.second = std::max(last.second, ranges[i].second);
            } else {
                merged.push_back(ranges[i]);
            }
        }
        return merged;
    }

    std::vector<Label> getI2QueryLabels(const std::vector<std::pair<int, int>>& pos_ranges) const {
        std::vector<Label> labels;
        if (!pos_tree_) {
            return labels;
        }
        labels.reserve(pos_ranges.size());
        std::unordered_set<Label> seen_labels;
        seen_labels.reserve(pos_ranges.size());
        for (auto range : pos_ranges) {
            range.first = std::max(range.first, pos_full_range_.first);
            range.second = std::min(range.second, pos_full_range_.second);
            if (range.first > range.second) {
                continue;
            }
            Label label = i2Label(pos_tree_->get_single_range_cover(range));
            if (seen_labels.insert(label).second) {
                labels.push_back(std::move(label));
            }
        }
        return labels;
    }

    std::size_t entryCount() const {
        return entry_count_;
    }

    std::size_t uniqueValueCount() const {
        return range_table_.size();
    }

private:
    static int heightForDomain(int domain_size) {
        if (domain_size <= 1) {
            return 0;
        }
        int h = 0;
        int size = 1;
        while (size < domain_size) {
            size <<= 1;
            ++h;
        }
        return h;
    }

    static std::pair<int, int> fullRangeForTree(int domain_size) {
        int h = heightForDomain(domain_size);
        return {0, (1 << h) - 1};
    }

    static std::string serializeRange(const std::pair<int, int>& range) {
        return std::to_string(range.first) + "-" + std::to_string(range.second);
    }

    std::string i1Label(const std::pair<int, int>& range) const {
        return prefix_ + ":i1:" + serializeRange(range);
    }

    std::string i2Label(const std::pair<int, int>& range) const {
        return prefix_ + ":i2:" + serializeRange(range);
    }

    std::string prefix_;
    std::shared_ptr<Tdag> attr_tree_;
    std::shared_ptr<Tdag> pos_tree_;
    std::pair<int, int> attr_full_range_{0, 0};
    std::pair<int, int> pos_full_range_{0, 0};
    std::vector<RangeMeta> range_table_;
    std::size_t entry_count_ = 0;
};
