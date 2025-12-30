#pragma once

#include "index_interface.hpp"
#include "quad_tree_3d_src.hpp"
#include <memory>

class QdagSRC3D : public Index_Interface {
public:
    QdagSRC3D(int max_x, int max_y, int max_z );
     // 修正：使用与Python一致的参数
    // explicit QdagSRC3D(int height, bool is_src = true);

    KeywordMap mapPointsToLabels(const PointMap3D& points) const override;
    
    std::vector<Label> getQueryLabels(const Rect3D& query_rect) const override;

    // 添加 getter 用于测试
    const QuadTree3DSRC* getQuadTree() const { return mutable_qtree_.get(); }

private:
    // 将树的构建与查询分离开，需要一个非const的树用于构建
    mutable std::unique_ptr<QuadTree3DSRC> mutable_qtree_; 
};