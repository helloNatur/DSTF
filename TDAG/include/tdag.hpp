#pragma once

#include <vector>
#include <utility>
#include <memory>
#include <string>

/**
 * @class Tdag
 * @brief Implements a Tdag (Ternary DAG-like tree) structure for 1D range covering.
 *
 * This data structure represents a hierarchical decomposition of a 1D coordinate space.
 * It is designed to efficiently find a single, pre-defined range ("cover") that
 * contains a given query range. It's a key component for the Tdag-SRC scheme.
 */
class Tdag : public std::enable_shared_from_this<Tdag> {
public:
    // 成员变量保持公开，以便于上层策略访问，符合其作为数据结构的定位
    std::shared_ptr<Tdag> left;
    std::shared_ptr<Tdag> right;
    std::pair<int, int> middle;
    std::pair<int, int> range;
    int height;

    /**
     * @brief Private constructor to build the tree recursively.
     * @param h Current node's height.
     * @param l Left bound of the current node's range.
     * @param r Right bound of the current node's range.
     */
    Tdag(int h, int l, int r);

    /**
     * @brief Factory function to initialize a Tdag of a given height.
     * @param height The height of the tree, determining the space size (2^height).
     * @return A shared pointer to the root of the initialized Tdag.
     */
    static std::shared_ptr<Tdag> initialize(int height);

    /**
     * @brief Finds the path of containing ranges from the root to a specific value.
     * @param val The integer value to trace.
     * @param rnge The initial full range of the coordinate space.
     * @return A vector of ranges that form the path.
     */
    std::vector<std::pair<int, int>> descend_tree(int val, std::pair<int, int> rnge) const;

    /**
     * @brief Finds the single canonical range that covers the query range.
     * @param query_range The range to be covered.
     * @return The smallest pre-defined node range in the Tdag that contains the query_range.
     */
    std::pair<int, int> get_single_range_cover(const std::pair<int, int>& query_range) const;

    /**
     * @brief Utility function to check if one interval contains another.
     * @param main The container interval.
     * @param secondary The contained interval.
     * @return True if main contains secondary, false otherwise.
     */
    bool interval_contains_interval(const std::pair<int, int>& main, const std::pair<int, int>& secondary) const;

private:
    

    /**
     * @brief Recursive helper for get_single_range_cover.
     * @param node The current node to inspect.
     * @param query_range The target query range.
     * @return The covering range found in the subtree rooted at `node`.
     */
    std::pair<int, int> get_single_range_cover_helper(const std::shared_ptr<const Tdag>& node, const std::pair<int, int>& query_range) const;
    
    
};