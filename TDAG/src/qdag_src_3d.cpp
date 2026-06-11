#include "qdag_src_3d.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <unordered_set>


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
    std::size_t non_empty_cells = 0;
    std::size_t source_records = 0;
    std::size_t total_cell_label_edges = 0;
    std::size_t max_labels_per_cell = 0;

    for (const auto& [point, ids] : points) {
        if (ids.empty()) continue;
        ++non_empty_cells;
        source_records += ids.size();

        const auto covers = mutable_qtree_->findContainingRangeCovers(point);
        std::unordered_set<std::string> unique_labels;
        unique_labels.reserve(covers.size());
        for (const auto& cover : covers) {
            unique_labels.insert(cover.toLabelString());
        }

        const std::size_t labels_for_cell = unique_labels.size();
        total_cell_label_edges += labels_for_cell;
        max_labels_per_cell = std::max(max_labels_per_cell, labels_for_cell);

        for (const auto& label : unique_labels) {
            auto& postings = modified_mm[label];
            postings.insert(postings.end(), ids.begin(), ids.end());
        }
    }

    std::size_t total_postings = 0;
    std::size_t max_postings_per_label = 0;
    for (const auto& [label, ids] : modified_mm) {
        total_postings += ids.size();
        max_postings_per_label = std::max(max_postings_per_label, ids.size());
    }

    const double avg_labels_per_cell = non_empty_cells == 0
        ? 0.0
        : static_cast<double>(total_cell_label_edges) / static_cast<double>(non_empty_cells);
    const double avg_records_per_cell = non_empty_cells == 0
        ? 0.0
        : static_cast<double>(source_records) / static_cast<double>(non_empty_cells);
    const double avg_postings_per_label = modified_mm.empty()
        ? 0.0
        : static_cast<double>(total_postings) / static_cast<double>(modified_mm.size());

    std::cout << std::fixed << std::setprecision(3)
              << "[QdagSRC3DStats] source_records=" << source_records
              << " non_empty_grid_cells=" << non_empty_cells
              << " labels=" << modified_mm.size()
              << " total_cell_label_edges=" << total_cell_label_edges
              << " total_postings=" << total_postings
              << " avg_records_per_cell=" << avg_records_per_cell
              << " avg_labels_per_cell=" << avg_labels_per_cell
              << " max_labels_per_cell=" << max_labels_per_cell
              << " avg_postings_per_label=" << avg_postings_per_label
              << " max_postings_per_label=" << max_postings_per_label
              << std::endl;

    return modified_mm;
}

std::vector<Label> QdagSRC3D::getQueryLabels(const Rect3D& query_rect) const {
    Rect3D cover = mutable_qtree_->getSingleRangeCover(query_rect);
    std::string label = cover.toLabelString();
    return {label};
}