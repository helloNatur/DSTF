#pragma once

#include <vector>
#include <utility>
#include <memory>
#include <string>
#include <bf/bloom_filter/basic.hpp>  // Add Bloom Filter header
#include <limits>  // For numeric_limits

/**
 * @class TdagBF
 * @brief Implements a Tdag (Ternary DAG-like tree) structure for 1D range covering with BF integration.
 *
 * This data structure represents a hierarchical decomposition of a 1D coordinate space.
 * Each node has a Bloom Filter (BF) for spatial keywords (cube codes) in its subtree.
 * Leaves store BF directly; internal nodes aggregate via OR-merge from children.
 * Merging shares BF objects if Hamming distance < THRESHOLD to reduce count from O(n) to O(k).
 * It's a key component for the TdagBF-SRC scheme with pruning.
 */
class TdagBF : public std::enable_shared_from_this<TdagBF> {
public:
    // 成员变量保持公开，以便于上层策略访问，符合其作为数据结构的定位
    std::shared_ptr<TdagBF> left;
    std::shared_ptr<TdagBF> right;
    std::pair<int, int> middle;
    std::pair<int, int> range;
    int height;
    std::shared_ptr<bf::basic_bloom_filter> bf;  // BF for spatial keywords in subtree (shared for merges)
    std::shared_ptr<TdagBF> parent;  // Weak ptr to parent for upward merge (add this line)

    // BF parameters (from SegmentTree)
    double fp;
    size_t capacity;
    size_t seed;
    bool double_hashing;
    bool partition;
    bf::hasher global_hasher; // 全局哈希函数，确保所有bf使用相同的哈希函数
    size_t required_cells;
    size_t optimal_k;

    /**
     * @brief Private constructor to build the tree recursively, init BF.
     * @param h Current node's height.
     * @param l Left bound of the current node's range.
     * @param r Right bound of the current node's range.
     * @param fp BF false positive rate.
     * @param capacity Expected insertions.
     * @param seed BF seed.
     */
    TdagBF(int h, int l, int r, double fp = 0.01, size_t capacity = 1000, size_t seed = 0, bool double_hashing = true, bool partition = true);

    /**
     * @brief Factory function to initialize a TdagBF of a given height with BF params.
     * @param height The height of the tree, determining the space size (2^height).
     * @param fp BF false positive rate.
     * @param capacity Expected insertions per node.
     * @param seed BF seed.
     * @return A shared pointer to the root of the initialized TdagBF.
     */
    static std::shared_ptr<TdagBF> initialize(int height=8, double fp = 0.01, size_t capacity = 1000, size_t seed = 0);

    /**
     * @brief Finds the path of containing ranges from the root to a specific value.
     * @param val The integer value to trace.
     * @param rnge The initial full range of the coordinate space.
     * @return A vector of ranges that form the path.
     */
    std::vector<std::pair<int, int>> descend_tree(int val, std::pair<int, int> rnge) const;

    /**
     * @brief Insert a spatial keyword (cube code) into leaf BFs along the path to a time interval.
     * Only adds to leaves; propagates OR-merge upward to internals.
     * @param interval_start Start of time interval.
     * @param interval_end End of time interval.
     * @param keyword The spatial cube code to insert.
     */
    void insert_keyword(int interval_start, int interval_end, const std::vector<std::string>& keywords);

    /**
     * @brief Insert keywords into matching leaf BFs without propagating to parents.
     *
     * Use this for bulk construction, then call finalize_bloom_filters() once.
     * It avoids rebuilding the same parent BFs for every inserted record.
     */
    void insert_keyword_deferred(int interval_start,
                                 int interval_end,
                                 const std::vector<std::string>& keywords);

    /**
     * @brief Rebuild internal Bloom Filters bottom-up after deferred inserts.
     */
    void finalize_bloom_filters();

    // ✅ 旧接口：单关键词（为兼容现有测试与旧调用）
    void insert_keyword(int interval_start, int interval_end, const std::string& keyword);

    /**
     * @brief Check if any keyword matches in the node's BF (pruning).
     * @param keywords Query spatial cube codes.
     * @return True if any keyword possibly in BF.
     */
    bool bf_matches(const std::vector<std::string>& keywords) const;

    /**
     * @brief 递归设置子节点的 parent（在树构建后调用）。
     */
    void build_parents();

    /**
     * @brief Finds the single canonical range that covers the query range, with BF pruning.
     * @param query_range The range to be covered.
     * @param keywords Query spatial cube codes for BF check.
     * @return The smallest pre-defined node range that contains query_range and BF-matches keywords.
     * Throws if no valid cover found.
     */
    std::pair<int, int> get_single_range_cover(const std::pair<int, int>& query_range, const std::vector<std::string>& keywords) const;

    /**
     * @brief Utility function to check if one interval contains another.
     * @param main The container interval.
     * @param secondary The contained interval.
     * @return True if main contains secondary, false otherwise.
     */
    bool interval_contains_interval(const std::pair<int, int>& main, const std::pair<int, int>& secondary) const;

    // Getter for BF
    const std::shared_ptr<bf::basic_bloom_filter>& get_bf() const { return bf; }

    // Static threshold for Hamming distance merging
    static constexpr double THRESHOLD = 0.001;


    /**
     * @brief Recursive helper for get_single_range_cover with BF pruning.
     * @param node The current node to inspect.
     * @param query_range The target query range.
     * @param keywords Query keywords for BF check.
     * @return The covering range found in the subtree rooted at `node`, or {-1,-1} if no match.
     */
    std::pair<int, int> get_single_range_cover_helper(const std::shared_ptr<const TdagBF>& node, 
                                                      const std::pair<int, int>& query_range, 
                                                      const std::vector<std::string>& keywords) const;

    /**
     * @brief Recursive insert keyword into leaf BFs; upward OR-merge to internals.
     * @param node Current node.
     * @param interval_start Start of interval.
     * @param interval_end End of interval.
     * @param keyword Keyword to insert.
     */
    void insert_keyword_helper(std::shared_ptr<TdagBF> node, int interval_start, int interval_end, const std::vector<std::string>& keywords);

    void insert_keyword_leaf_only_helper(std::shared_ptr<TdagBF> node,
                                         int interval_start,
                                         int interval_end,
                                         const std::vector<std::string>& keywords);

    void finalize_bloom_filters_helper(std::shared_ptr<TdagBF> node);

    void reset_bloom_filter(const std::shared_ptr<bf::basic_bloom_filter>& target) const;

    void or_assign_bloom(const std::shared_ptr<bf::basic_bloom_filter>& target,
                         const std::shared_ptr<bf::basic_bloom_filter>& source) const;

    /**
     * @brief Upward propagation: Merge children's BF into parent's via OR.
     * Checks Hamming distance; shares if < THRESHOLD.
     * @param node Current node to update parent.
     */
    void update_parent(std::shared_ptr<TdagBF> node);

    /**
     * @brief Compute Hamming distance between two BFs.
     * @param bf1 First BF.
     * @param bf2 Second BF.
     * @return Distance or max if null.
     */
    size_t hamming_distance(const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
                            const std::shared_ptr<bf::basic_bloom_filter>& bf2) const;

    /**
     * @brief Merge two BFs via OR; return shared new/updated BF.
     * @param bf1 First BF.
     * @param bf2 Second BF.
     * @return Merged shared BF (shares if possible).
     */
    std::shared_ptr<bf::basic_bloom_filter> merge_bloom(
        const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
        const std::shared_ptr<bf::basic_bloom_filter>& bf2);


    // 把区间映射到 SRC 单节点，并把 keyword 直接加到该节点 BF（不走叶子路径）
    void update_src_cover(const std::pair<int,int>& query_range,
                            const std::string& keyword);

    // 返回：从根到叶的一条“包含链”上，所有覆盖该 leaf 的 TDAG 结点区间（含 middle）
    std::vector<std::pair<int,int>> covering_intervals_for_leaf(int leaf) const;

    bool has_match_within(const std::shared_ptr<const TdagBF>& node,
                              std::pair<int,int> q,
                              const std::vector<std::string>& kws) const;

    
};
