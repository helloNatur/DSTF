#ifndef NON_SECURE_CCS_H
#define NON_SECURE_CCS_H

#include <vector>
#include <string>

//无加密
class CubeCode {
private:
    int d; //维度
    std::vector<double> min_values; // 每个维度的最小值
    std::vector<double> max_values; // 每个维度的最大值
    int k; //级别数

    //生成立方体编码（无加密）
    std::string generateCubeCode(const std::vector<double>& center,int level) const;

    //递归收集查询范围的立方体编码
    void collectCubeCodes(int current_level, 
                          const std::vector<double>& current_min,
                          const std::vector<double>& current_max, 
                          const std::vector<double>& query_min,
                          const std::vector<double>& query_max,
                          std::vector<std::string>& cubes) const;

public:
    CubeCode(int dimensions, const std::vector<double>& min_vals, const std::vector<double>& max_vals, int levels);

    // 为数据记录生成立方体编码
    std::vector<std::string> generateDataCubeCodes(const std::vector<double>& point) const;

    // 为查询范围生成立方体编码
    std::vector<std::string> generateQueryCubeCodes(const std::vector<double>& query_min,
                                                   const std::vector<double>& query_max) const;

    //getter方法
    int getDimensions() const { return d; }
    const std::vector<double>& getMinValues() const { return min_values; }
    const std::vector<double>& getMaxValues() const { return max_values; }
    int getLevels() const { return k; }
    
};

#endif // NON_SECURE_CCS_H