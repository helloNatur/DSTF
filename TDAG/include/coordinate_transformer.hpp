#pragma once

#include "spatiotemporal_point.hpp"
#include "grid_point_3d.hpp"
#include "grid_rect_3d.hpp"
#include <cmath>

// 定义时空查询范围
struct SpatiotemporalQueryBox {
    long long min_ts, max_ts;
    double min_lat, max_lat;
    double min_lon, max_lon;
};

class CoordinateTransformer {
    // 把一个实数（如时间差、纬度、经度）按指定范围归一化并映射到网格坐标。
private:
    int transform(double value, double range) const {
        if (range == 0) return 0;
        double normalized = value / range;
        int grid_val = static_cast<int>(normalized * grid_max_val_);
        // 边界检查
        if (grid_val < 0) return 0;
        if (grid_val > grid_max_val_) return grid_max_val_;
        return grid_val;
    }

    int transform_end(double value, double range) const {
        if (range == 0) return grid_size_;
        double normalized = value / range;
        int grid_val = static_cast<int>(std::ceil(normalized * grid_max_val_));
        if (grid_val < 0) return 0;
        if (grid_val > grid_size_) return grid_size_;
        return grid_val;
    }

    long long min_ts_;
    double ts_range_;
    const int grid_size_;
    const int grid_max_val_;

public:
    CoordinateTransformer(long long min_ts, long long max_ts, unsigned int grid_bits)
        : min_ts_(min_ts),
          ts_range_((double)max_ts - min_ts),
          grid_size_(1 << grid_bits),
          grid_max_val_(grid_size_ - 1) {}

    // 把一个单点（时间戳 + 经纬度）转换成三维网格坐标 (x,y,z)。
    // 时间：映射到区间 [0, grid_max_val]
    // 纬度：原始范围是 [-90, 90]，先平移到 [0,180] 再映射。
    // 经度：原始范围是 [-180, 180]，先平移到 [0,360] 再映射。
    GridPoint3D to_grid_point(const SpatiotemporalPoint& st_point) const {
        int x = transform(st_point.utc_timestamp - min_ts_, ts_range_);
        int y = transform(st_point.latitude + 90.0, 180.0);
        int z = transform(st_point.longitude + 180.0, 360.0);
        return {x, y, z};
    }
    
    // 把一个查询范围（时间 + 经纬度范围）转换成一个三维“长方体”网格区域 Rect3D。
    Rect3D to_grid_rect(const SpatiotemporalQueryBox& query_box) const {
        // 转换查询范围的起始点和结束点
        // 纬度范围: [-90.0, 90.0]
        // 经度范围: [-180.0, 180.0]
        GridPoint3D start(
            transform(query_box.min_ts - min_ts_, ts_range_),
            transform(query_box.min_lat + 90.0, 180.0),
            transform(query_box.min_lon + 180.0, 360.0)
        );
        GridPoint3D end(
            transform_end(query_box.max_ts - min_ts_, ts_range_),
            transform_end(query_box.max_lat + 90.0, 180.0),
            transform_end(query_box.max_lon + 180.0, 360.0)
        );
        return {start, end};
    }


};
