#include "tdag_src_3d.hpp"
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <iostream>

TdagSRC3D::TdagSRC3D(int max_x, int max_y, int max_z)
    : max_x_(max_x), max_y_(max_y), max_z_(max_z) {
    if (max_x <= 0 || max_y <= 0 || max_z <= 0) {
        throw std::invalid_argument("Max dimensions must be positive.");
    }
    int x_height = static_cast<int>(std::ceil(std::log2(max_x_)));
    int y_height = static_cast<int>(std::ceil(std::log2(max_y_)));
    int z_height = static_cast<int>(std::ceil(std::log2(max_z_)));

    // 对X, Y, Z三个维度，分别初始化一个 Tdag 树
    x_tree_ = Tdag::initialize(x_height);
    y_tree_ = Tdag::initialize(y_height);
    z_tree_ = Tdag::initialize(z_height);
}

// 构建索引
// 生成一个巨大的 KeywordMap。一个点会被映射到多个标签下，这是为了确保无论查询生成的覆盖范围有多“粗糙”，都能找到这个点。
// 对于数据库中的每一个三维点 P(x, y, z)，它会调用 descend_tree 方法。
// 这个方法会找出在 Tdag 树中，从根节点到包含该点 x（或 y, z）的叶子节点路径上所有经过的节点范围。
// 然后，它将X、Y、Z三个维度上找到的所有路径节点范围进行笛卡尔积组合。
// 例如，如果x坐标被 x_range1, x_range2 覆盖，y坐标被 y_range1, y_range2 覆盖，
// z坐标被 z_range1, z_range2 覆盖，那么就会生成 2x2x2=8 个三元组标签，
// 如 (x_range1, y_range1, z_range1), (x_range1, y_range1, z_range2) 等
KeywordMap TdagSRC3D::mapPointsToLabels(const PointMap3D& points) const {
    KeywordMap modified_mm;
    std::pair<int, int> full_x_range = {0, (1 << static_cast<int>(std::ceil(std::log2(max_x_)))) - 1};//
    std::pair<int, int> full_y_range = {0, (1 << static_cast<int>(std::ceil(std::log2(max_y_)))) - 1};
    std::pair<int, int> full_z_range = {0, (1 << static_cast<int>(std::ceil(std::log2(max_z_)))) - 1};

    for (const auto& [point, vals] : points) {
        if (vals.empty()) continue;

        auto x_paths = x_tree_->descend_tree(point.x, full_x_range);
        auto y_paths = y_tree_->descend_tree(point.y, full_y_range);
        auto z_paths = z_tree_->descend_tree(point.z, full_z_range);

        // Cartesian product of all path nodes for each dimension
        // 点 P 关联的数据值会被添加到所有这些组合标签下。
        for (const auto& x_r : x_paths) {
            for (const auto& y_r : y_paths) {
                for (const auto& z_r : z_paths) {
                    std::string label = serialize_cover(x_r, y_r, z_r);
                    modified_mm[label].insert(modified_mm[label].end(), vals.begin(), vals.end());
                }
            }
        }
    }
    return modified_mm;
}

std::vector<Label> TdagSRC3D::getQueryLabels(const Rect3D& query_rect) const {
    // The "Single Range Cover" (SRC) strategy generates exactly one label.
    auto x_cover = x_tree_->get_single_range_cover({query_rect.start.x, query_rect.end.x});
    auto y_cover = y_tree_->get_single_range_cover({query_rect.start.y, query_rect.end.y});
    auto z_cover = z_tree_->get_single_range_cover({query_rect.start.z, query_rect.end.z});
    
    return { serialize_cover(x_cover, y_cover, z_cover) };
}

std::string TdagSRC3D::serialize_cover(
    const std::pair<int, int>& x_r,
    const std::pair<int, int>& y_r,
    const std::pair<int, int>& z_r) const {
    
    std::stringstream ss;
    ss << x_r.first << "," << x_r.second << "|" 
       << y_r.first << "," << y_r.second << "|" 
       << z_r.first << "," << z_r.second;
    return ss.str();
}