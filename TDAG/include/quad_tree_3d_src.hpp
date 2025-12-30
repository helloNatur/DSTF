#pragma once

#include "grid_point_3d.hpp"
#include "grid_rect_3d.hpp"
#include <vector>
#include <memory>
#include <string>
#include <functional>

// 为简洁定义类型别名
using PointRecord = std::pair<GridPoint3D, int>;

class QuadTree3DSRC {
private:
    int max_domain_;
    Rect3D root_rect_;
    bool is_src_ = true; // staggered if true
    std::unordered_map<Rect3D, std::vector<Rect3D>> qdag_dict_; // 数据无关 dict

    void buildQuadTreeIterative(); // 预建树

    std::vector<Rect3D> getChildren(const Rect3D& parent) const; // 统一标准/交错

public:
    QuadTree3DSRC(int height, bool is_src = true); // 用 height，如 Python

    std::vector<Rect3D> findContainingRangeCovers(const GridPoint3D& point) const; // 如 Python

    Rect3D getSingleRangeCover(const Rect3D& query) const; // SRC offset 逻辑

    // 添加 getter 用于测试
    const Rect3D& getRootRect() const { return root_rect_; }
    int getMaxDomain() const { return max_domain_; }
};