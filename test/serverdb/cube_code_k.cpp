#include "cube_code.h"
#include <cmath>
#include <sstream>      
#include <iomanip>
#include <stdexcept>

CubeCode::CubeCode(int dimensions, const std::vector<double>& min_vals, const std::vector<double>& max_vals, int levels)
    : d(dimensions), min_values(min_vals), max_values(max_vals), k(levels) {
    if (min_values.size() != d || max_values.size() != d) {
        throw std::invalid_argument("Min and max values must match the number of dimensions.");
    }
    if (levels < 1) {
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

/**
 * @brief 为单个数据点生成编码。
 * 修改后：只生成该点所属的最深层（第 k 层）的立方体编码。
 * 
 * @param point 数据点坐标。
 * @return 包含一个字符串的向量，即第 k 层的编码。
 */
std::vector<std::string> CubeCode::generateDataCubeCodes(const std::vector<double>& point) const {
    if (point.size() != d) {
        throw std::invalid_argument("Point dimensions do not match cube dimensions.");
    }

    std::vector<double> current_min = min_values;
    std::vector<double> current_max = max_values;

    // 循环下沉到第 k 层，以确定点所在的最小立方体
    for (int level = 1; level <= k; ++level) {
        std::vector<double> center(d);
        std::vector<double> next_min(d);
        std::vector<double> next_max(d);

        for (int i = 0; i < d; ++i) {
            center[i] = (current_min[i] + current_max[i]) / 2.0;
            if (point[i] < center[i]) {
                next_min[i] = current_min[i];
                next_max[i] = center[i];
            } else {
                next_min[i] = center[i];
                next_max[i] = current_max[i];
            }
        }
        current_min = next_min;
        current_max = next_max;
    }

    // 计算第 k 层立方体的中心点
    std::vector<double> final_center(d);
    for (int i = 0; i < d; ++i) {
        final_center[i] = (current_min[i] + current_max[i]) / 2.0;
    }

    // 只返回第 k 层的编码
    return {generateCubeCode(final_center, k)};
}


/**
 * @brief 递归辅助函数，用于收集与查询范围相交的立方体编码。
 * 修改后：总是递归到第 k 层，只收集第 k 层的编码。
 */
void CubeCode::collectCubeCodes(int current_level,
                                const std::vector<double>& current_min,
                                const std::vector<double>& current_max,
                                const std::vector<double>& query_min,
                                const std::vector<double>& query_max,
                                std::vector<std::string>& cubes) const {
    // 剪枝：如果当前立方体与查询范围完全不相交，则返回
    bool overlap = true;
    for (int i = 0; i < d; ++i) {
        if (current_max[i] <= query_min[i] || current_min[i] >= query_max[i]) {
            overlap = false;
            break;
        }
    }
    if (!overlap) {
        return;
    }

    // 基本情况：已经到达目标层级 k
    if (current_level == k) {
        // 由于已经通过了重叠检查，直接添加该立方体的编码
        std::vector<double> center(d);
        for (int i = 0; i < d; ++i) {
            center[i] = (current_min[i] + current_max[i]) / 2.0;
        }
        cubes.push_back(generateCubeCode(center, k));
        return;
    }

    // 递归步骤：如果未达到第 k 层，则继续向下划分
    std::vector<double> center(d);
    for (int i = 0; i < d; ++i) {
        center[i] = (current_min[i] + current_max[i]) / 2.0;
    }

    // 遍历所有 2^d 个子立方体
    for (int i = 0; i < (1 << d); ++i) {
        std::vector<double> next_min(d);
        std::vector<double> next_max(d);
        for (int j = 0; j < d; ++j) {
            if ((i >> j) & 1) { // j-th bit is 1
                next_min[j] = center[j];
                next_max[j] = current_max[j];
            } else { // j-th bit is 0
                next_min[j] = current_min[j];
                next_max[j] = center[j];
            }
        }
        // 对子立方体进行递归调用
        collectCubeCodes(current_level + 1, next_min, next_max, query_min, query_max, cubes);
    }
}

/**
 * @brief 为一个查询范围生成编码。
 * 修改后：只返回与查询范围相交的所有第 k 层的编码。
 * 
 * @param query_min 查询范围的最小值坐标。
 * @param query_max 查询范围的最大值坐标。
 * @return 包含所有相交的第 k 层编码的字符串向量。
 */
std::vector<std::string> CubeCode::generateQueryCubeCodes(const std::vector<double>& query_min, const std::vector<double>& query_max) const {
    if (query_min.size() != d || query_max.size() != d) {
        throw std::invalid_argument("Query range dimensions do not match cube dimensions.");
    }
    std::vector<std::string> cubes;
    collectCubeCodes(1, min_values, max_values, query_min, query_max, cubes);
    return cubes;
}

// getCubeCode, getBPlusTree, getSegmentTree 方法的实现
// ... 如果有这些方法，请保留它们 ...
// 假设这些方法在其他地方定义或不需要修改
