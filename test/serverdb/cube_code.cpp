#include "cube_code.h"
#include <cmath>
#include <sstream>      
#include <iomanip>

// TODO:多维转为一维（比如经纬度转为一维cube）
// 确定范围： 确定每个维度的min和max
// 递归划分： 从根节点开始，递归地将每个立方体分为2^d个子立方体
// 编码生成： 为每个立方体生成唯一编码，这些编码需要添加到bf上
// 数据映射： 将每个记录映射到从根到叶子的路径上的所有立方体
// 查询处理： 如果查询范围完全包含某个立方体，则直接添加该立方体；部分重叠，则递归划分到更细粒度的子立方体，直至级别k或完全包含

// 查询处理： 将查询范围映射到立方体，找到覆盖范围的立方体，检索并过滤数据记录 ci和cj的交集不为空则可以添加
// HCE 把 d 维数据空间递归划分为 2**𝑑的子空间，用中心点和层级作为 cube 编码，从而将多维点和范围映射为一组一维字符串 code:40.625,-74.075,1

CubeCode::CubeCode(int dimensions, const std::vector<double>& min_vals, const std::vector<double>& max_vals, int levels)
    : d(dimensions), min_values(min_vals), max_values(max_vals), k(levels) {
    if (min_values.size() != d || max_values.size() != d) {
        throw std::invalid_argument("Min and max values must match the number of dimensions.");
    }
    if(levels<1){
        throw std::invalid_argument("Levels must be at least 1.");
    }
}

std::string CubeCode::generateCubeCode(const std::vector<double>& center, int level) const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(6); // 设置 6 位小数精度
    for (size_t i = 0; i < center.size(); ++i) {
        ss << center[i];
        if (i < center.size() - 1) {
            ss << ",";
        }
    }
    ss << "," << level;
    return ss.str();
}

void CubeCode::collectCubeCodes(int current_level, 
                                const std::vector<double>& current_min,
                                const std::vector<double>& current_max, 
                                const std::vector<double>& query_min,
                                const std::vector<double>& query_max,
                                std::vector<std::string>& cubes) const {
    if (current_level > k) return;

    // 查询范围包含当前立方体
    bool inside = true;
    for (int i = 0; i < d; ++i) {
        if (query_min[i] > current_min[i] || query_max[i] < current_max[i]) { //第一次为第一层的立方体
            inside = false;
            break;
        }
    }


    if (inside) {
        std::vector<double> center(d);
        for (int i = 0; i < d; ++i) {
            center[i] = (current_min[i] + current_max[i]) / 2.0;
        }
        cubes.push_back(generateCubeCode(center, current_level));//把中心点全部加入
        return;
    }

    // 检查当前立方体是否与查询范围重叠
    bool intersects = true;
    for (int i = 0; i < d; ++i) {
        if (current_max[i] <= query_min[i] || current_min[i] >= query_max[i]) {
            intersects = false;
            break;
        }
    }

    if (!intersects) return;

    

    // 如果当前级别小于 k，递归处理子立方体
    if (current_level < k) {
        std::vector<double> mids(d);
        for (int i = 0; i < d; ++i) {
            mids[i] = (current_min[i] + current_max[i]) / 2.0;
        }
        int num_children = 1 << d; // 2^d
        for (int c = 0; c < num_children; ++c) {
            std::vector<double> child_min(d), child_max(d);
            for (int i = 0; i < d; ++i) {
                if ((c & (1 << i)) == 0) { // 下半部分
                    child_min[i] = current_min[i];
                    child_max[i] = mids[i];
                } else { // 上半部分
                    child_min[i] = mids[i];
                    child_max[i] = current_max[i];
                }
            }
            collectCubeCodes(current_level + 1, child_min, child_max, query_min, query_max, cubes);
        }
    } else {
        // 如果当前级别为 k，添加当前立方体的编码
        std::vector<double> center(d);
        for (int i = 0; i < d; ++i) {
            center[i] = (current_min[i] + current_max[i]) / 2.0;
        }
        cubes.push_back(generateCubeCode(center, current_level));
    }
}

std::vector<std::string> CubeCode::generateDataCubeCodes(const std::vector<double>& point) const {
    if (point.size() != d) {
        throw std::invalid_argument("Point must have d dimensions");
    }
    std::vector<std::string> codes;
    int start_level = 1; 

    //检查是否为边界点，边界点仅返回level=1的编码
    // 检查是否为边界点
    bool is_boundary = true;
    for (int i = 0; i < d; ++i) {
        if (point[i] != min_values[i] && point[i] != max_values[i]) {
            is_boundary = false;
            break;
        }
    }
    if (is_boundary) {
        // 为边界点计算第一级划分的中心
        std::vector<double> cube_min = min_values;
        std::vector<double> cube_max = max_values;
        std::vector<double> mids(d);
        for (int i = 0; i < d; ++i) {
            mids[i] = (cube_min[i] + cube_max[i]) / 2.0; // 第一级中间点
        }
        std::vector<double> center(d);
        for (int i = 0; i < d; ++i) {
            // 边界点 {40.5, -74.2} 落入左下角子立方体
            if (point[i] == min_values[i]) {
                center[i] = (cube_min[i] + mids[i]) / 2.0;
            } else { // max_values 情况（当前测试无此情况）
                center[i] = (mids[i] + cube_max[i]) / 2.0;
            }
        }
        codes.push_back(generateCubeCode(center, 1));
        return codes; // 仅返回 level=1 的编码
    }

    //正常情况，为每个级别生成编码
    for (int l = start_level; l <= k; ++l) {
        std::vector<double> cube_min(d), cube_max(d);
        for (int i = 0; i < d; ++i) {
            double step = (max_values[i] - min_values[i]) / (1 << l);
            int pi = static_cast<int>(std::floor((point[i] - min_values[i]) / step));
            if (pi < 0) pi = 0;
            if (pi >= (1 << l)) pi = (1 << l) - 1;
            cube_min[i] = min_values[i] + pi * step;
            cube_max[i] = cube_min[i] + step;
        }
        std::vector<double> center(d);
        for (int i = 0; i < d; ++i) {
            center[i] = (cube_min[i] + cube_max[i]) / 2.0;
        }
        codes.push_back(generateCubeCode(center, l));
    }
    return codes;
}

std::vector<std::string> CubeCode::generateQueryCubeCodes(const std::vector<double>& query_min, const std::vector<double>& query_max) const {
    if (query_min.size() != d || query_max.size() != d) {
        throw std::invalid_argument("Query ranges must have d dimensions");
    }
    std::vector<std::string> result;
    int start_level = 1; // 从第2级开始
    collectCubeCodes(start_level-1, min_values, max_values, query_min, query_max, result);
    return result;
}