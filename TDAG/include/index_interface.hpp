#ifndef INDEX_INTERFACE_HPP
#define INDEX_INTERFACE_HPP
// 索引策略接口
// #include "point_3d.hpp"
// #include "rect_3d.hpp"
#include "grid_point_3d.hpp"
#include "grid_rect_3d.hpp"
#include "emm_interface.hpp" // 引用类型别名


// 定义3D点数据映射类型
// using PointMap3D = std::unordered_map<Point3D, std::vector<RecordID>, Point3D::Hash>;
using PointMap3D = std::unordered_map<GridPoint3D, std::vector<RecordID>, GridPoint3D::Hash>;

class Index_Interface {
public:
    virtual ~Index_Interface() = default;

    // 核心职责1: 将3D点数据映射为关键字->ID列表的形式
    virtual KeywordMap mapPointsToLabels(const PointMap3D& points) const = 0;

    // 核心职责2: 将一个范围查询转换为一个或多个关键字
    virtual std::vector<Label> getQueryLabels(const Rect3D& query_rect) const = 0;
};

#endif