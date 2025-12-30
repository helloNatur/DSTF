#include "qdag_src_3d.hpp"


// 建树：固定八叉树 + SRC 节点（27 cube）。
// 点 → 标签：点对应 root → leaf 的所有 canonical cube。
// 查询 → 标签：找到一个最小的 canonical cube 覆盖查询
QdagSRC3D::QdagSRC3D(int max_x, int max_y, int max_z) {
    // 计算 h 如 Python
    int h_x = std::ceil(std::log2(max_x));
    int h_y = std::ceil(std::log2(max_y));
    int h_z = std::ceil(std::log2(max_z));
    int h = std::max({h_x, h_y, h_z});
    mutable_qtree_ = std::make_unique<QuadTree3DSRC>(h, true); // SRC mode
}

KeywordMap QdagSRC3D::mapPointsToLabels(const PointMap3D& points) const {
    KeywordMap modified_mm;
    for (const auto& [point, ids] : points) {
        if (ids.empty()) continue;
        auto covers = mutable_qtree_->findContainingRangeCovers(point);
        for (const auto& cover : covers) {
            std::string label = cover.toLabelString();
            modified_mm[label].insert(modified_mm[label].end(), ids.begin(), ids.end());
        }
    }
    return modified_mm;
}

std::vector<Label> QdagSRC3D::getQueryLabels(const Rect3D& query_rect) const {
    Rect3D cover = mutable_qtree_->getSingleRangeCover(query_rect);
    std::string label = cover.toLabelString();
    return {label};
}