#include "tdag.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>

// Private constructor
// 对一维坐标空间进行一种特殊的、分层的划分
// 二叉树，但每个节点额外带一个middle区间
Tdag::Tdag(int h, int l, int r)
    : height(h), range({l, r}), middle({-1, -1}) {
    if (h <= 0) {
        return;
    }

    int midpoint = l + (r - l) / 2; // Avoid potential overflow vs (l+r)/2
    int quarter_range = (r - l) / 4;

    // The special middle node calculation, consistent with the Python logic
    if ((r - l) > 1) {
        int mid0 = midpoint - quarter_range;
        int mid1 = midpoint + quarter_range + ((r - l) % 4 > 0 ? 1 : 0);
        middle = {mid0, mid1};
    }

    left = std::make_shared<Tdag>(h - 1, l, midpoint);
    if (midpoint + 1 <= r) {
        right = std::make_shared<Tdag>(h - 1, midpoint + 1, r);
    }
}

// Static factory method
std::shared_ptr<Tdag> Tdag::initialize(int height) {
    if (height < 0) {
        return nullptr;
    }
    int max_val = (1 << height) - 1;
    // The constructor is private, so we must call it from within the class
    return std::make_shared<Tdag>(height, 0, max_val);
}

// 构建索引
// 从根节点开始，沿着树向下遍历，直到找到包含该值的叶子节点
// 在这个过程中，记录下所有经过的节点范围
// 返回这些范围的列表
std::vector<std::pair<int, int>> Tdag::descend_tree(int val, std::pair<int, int> rnge) const {
    std::vector<std::pair<int, int>> rnges;
    while (rnge.first != val || rnge.second != val) {
        if (std::find(rnges.begin(), rnges.end(), rnge) == rnges.end()) {
            rnges.push_back(rnge);//还没缩到叶子 [val,val] 时循环。把当前二叉树节点区间（rnge）加入结果（用 std::find 去重）。
        }

        int middle_val = rnge.first + (rnge.second - rnge.first) / 2;
        int quarter = (rnge.second - rnge.first) / 4;
        int mid0 = middle_val - quarter;
        int mid1 = middle_val + quarter + ((rnge.second - rnge.first) % 4 > 0 ? 1 : 0);

        if (val >= mid0 && val <= mid1 && (rnge.second - rnge.first) > 1) {
            std::pair<int, int> mid_range = {mid0, mid1};
            if (std::find(rnges.begin(), rnges.end(), mid_range) == rnges.end()) {
                rnges.push_back(mid_range);
            }
        }

        if (val <= middle_val) {
            rnge.second = middle_val;
        } else {
            rnge.first = middle_val + 1;
        }
    }
    if (std::find(rnges.begin(), rnges.end(), rnge) == rnges.end()) {
        rnges.push_back({val, val});
    }

    // std::sort(rnges.begin(), rnges.end());
    // auto it = std::unique(rnges.begin(), rnges.end());
    // rnges.resize(it - rnges.begin());

    return rnges;
}

// 用于查询阶段
// 给定一个查询范围（如 [x_start, x_end]），该函数会从树中寻找一个最小的、预定义的节点范围，
// 能够完全包含这个查询范围。这个策略的优点是，无论实际查询范围多大或多小，只要它们被同一个节点覆盖，
// 生成的陷门就是相同的。这降低了查询的复杂度（从多个范围查询变为单个标签查询），
// 但也造成了一定的信息泄漏（服务器会知道你的查询属于哪个预定义的粗粒度范围）。
std::pair<int, int> Tdag::get_single_range_cover(const std::pair<int, int>& query_range) const {
    // We pass a const shared_ptr of `this` to the helper
    auto res = get_single_range_cover_helper(shared_from_this(), query_range);
    if (res.first == -1) {
        throw std::runtime_error("No cover found for range [" + std::to_string(query_range.first) + "," + std::to_string(query_range.second) + "]");
    }
    return res;
}

std::pair<int, int> Tdag::get_single_range_cover_helper(const std::shared_ptr<const Tdag>& node, const std::pair<int, int>& query_range) const {
    if (!node || !interval_contains_interval(node->range, query_range)) {
        return {-1, -1}; // Sentinel for "not found in this subtree"
    }

    // First, check if the special "middle" node is a better fit.
    if (node->middle.first != -1 && interval_contains_interval(node->middle, query_range)) {
        bool left_contains = node->left ? interval_contains_interval(node->left->range, query_range) : false;
        bool right_contains = node->right ? interval_contains_interval(node->right->range, query_range) : false;
        if (!left_contains && !right_contains) {
            return node->middle; // `middle` is the tightest fit among children.
        }
    }

    // If not, recurse down to the appropriate child.
    if (node->left && interval_contains_interval(node->left->range, query_range)) {
        return get_single_range_cover_helper(node->left, query_range);
    }
     if (node->right && interval_contains_interval(node->right->range, query_range)) {
        return get_single_range_cover_helper(node->right, query_range);
    }

    // If no child contains the query range, then the current node is the best cover.
    return node->range;
}

bool Tdag::interval_contains_interval(const std::pair<int, int>& main, const std::pair<int, int>& secondary) const {
    return main.first <= secondary.first && main.second >= secondary.second;
}