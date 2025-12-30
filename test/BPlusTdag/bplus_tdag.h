#pragma once
#include <memory>
#include <vector>
#include <utility>
#include <algorithm>
#include <optional>
#include <map>

#include "tdag_bf.h"    // ← 替换 SegmentTree
#include "cube_code.h"  // 仍然用于空间 cube code
#include "TimeUtil.h"

// 把时间维线段树 SegmentTree替换为TDAG-SRC（TdagBF）

struct TimeCandidate {
    long long day_ts;           // 天
    int left_interval;          // [0..143] 等
    int right_interval;
    // 可选：若你仍需要返回 tokens，可在此添加
    // std::vector<std::shared_ptr<std::vector<unsigned char>>> tokens;
};

class BPlusTdagNode {
public:
    bool isLeaf;
    std::vector<long long> keys;                       // day_ts 升序
    std::vector<std::shared_ptr<BPlusTdagNode>> children;

    // —— 关键变化：每个叶子的 payload 从 SegmentTree → TdagBF
    std::vector<std::shared_ptr<TdagBF>> day_tdag;

    std::shared_ptr<BPlusTdagNode> next;
    std::shared_ptr<BPlusTdagNode> parent;  // 统一 shared_ptr

    BPlusTdagNode(bool leaf=false) : isLeaf(leaf) {}

    int findKeyIndex(long long key) const {
        auto it = std::lower_bound(keys.begin(), keys.end(), key);
        return it - keys.begin();
    }
};

class BPlusTdag {
public:
    BPlusTdag(int d ,
              const std::vector<double>& min_vals,
              const std::vector<double>& max_vals,
              int levels);

    void insert(long long day_ts, std::shared_ptr<TdagBF> tdag);

    bool remove(long long day_ts);

    std::shared_ptr<TdagBF> search(long long day_ts) const;

    std::vector<std::pair<long long, std::shared_ptr<TdagBF>>>
    rangeSearch(long long start_day_ts, long long end_day_ts) const;

    bool update_point(long long day_ts, int interval,
                      const std::vector<std::string>& keywords);

    std::vector<TimeCandidate>
    query_time_candidates(const std::string& start_time, const std::string& end_time, 
        double lat_min, double lat_max, double lon_min, double lon_max) const;

    std::shared_ptr<CubeCode> getCubeCode() const { return ccs;}
    std::shared_ptr<TdagBF> findOrCreateTdag(long long day_ts);

private:
    size_t order_=3;
    std::shared_ptr<BPlusTdagNode> root_;
    std::shared_ptr<CubeCode> ccs;

    void insertInternal(long long day_ts, std::shared_ptr<TdagBF> tdag, std::shared_ptr<BPlusTdagNode> node);
    void splitNode(std::shared_ptr<BPlusTdagNode> node, std::shared_ptr<BPlusTdagNode> parent);
    std::shared_ptr<BPlusTdagNode> findLeafNode(long long day_ts) const;
    void removeInternal(long long day_ts, std::shared_ptr<BPlusTdagNode> node);
    void borrowOrMerge(std::shared_ptr<BPlusTdagNode> node, std::shared_ptr<BPlusTdagNode> parent, int index);
};