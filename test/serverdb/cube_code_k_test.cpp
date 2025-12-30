#include <gtest/gtest.h>
#include "cube_code.h"
#include <memory>
#include <algorithm>
#include <iostream>

class CubeCodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<double>& min_vals = {40.5, -74.2}; // 纬度最小值
        const std::vector<double>& max_vals = {41.0, -73.7}; // 纬度最大值
        // 初始化 2D CubeCode，纬度 [40.5, 41], 经度 [-74.2, -73.7], 3 级
        ccs = std::make_shared<CubeCode>(2, min_vals, max_vals, 3);
    }

    std::shared_ptr<CubeCode> ccs;
};

TEST_F(CubeCodeTest, GenerateDataCubeCodes) {
    // 测试点：(40.65, -74.2)
    // 注意：由于经度是-74.2，正好在边界上，根据实现(point[i] < center[i])，它会被分到右侧（>= center）的子空间。
    std::vector<double> point = {40.65, -74.2};
    auto codes = ccs->generateDataCubeCodes(point);
    
    // 新逻辑：只生成第 k 层的编码，所以大小应为 1
    ASSERT_EQ(codes.size(), 1); 

    // 打印 codes 内容
    std::cout << "Generated data code (level 3 only): " << codes[0] << std::endl;
    
    // 手动计算预期值:
    // L0: lat[40.5, 41.0], lon[-74.2, -73.7]
    // L1 center: (40.75, -73.95). point is in lower-left quadrant.
    // L1 cube: lat[40.5, 40.75], lon[-74.2, -73.95]
    // L2 center: (40.625, -74.075). point is in upper-left quadrant.
    // L2 cube: lat[40.625, 40.75], lon[-74.2, -74.075]
    // L3 center: (40.6875, -74.1375). point is in lower-left quadrant.
    // L3 cube: lat[40.625, 40.6875], lon[-74.2, -74.1375]
    // Final center of L3 cube: lat(40.656250), lon(-74.168750)
    std::string expected_code = "40.656250,-74.168750,3";
    
    EXPECT_EQ(codes[0], expected_code);
}

TEST_F(CubeCodeTest, GenerateQueryCubeCodes) {
    // 测试查询范围
    std::vector<double> query_min = {40.6, -74.1};
    std::vector<double> query_max = {40.7, -74.0};
    auto query_codes = ccs->generateQueryCubeCodes(query_min, query_max);

    // 查询结果不应为空
    ASSERT_FALSE(query_codes.empty());

    std::cout << "Generated query codes (level 3 only): " << std::endl;
    for (const auto& code : query_codes) {
        std::cout << "  " << code << std::endl;
        // 新逻辑：所有编码都必须是第 3 层的
        // code.back() 是 '3'
        EXPECT_EQ(code.back(), '3');
    }

    // 验证一个已知在范围内的点，其编码是否被查询结果所覆盖
    std::vector<double> point_in_range = {40.65, -74.05}; // 这个点在查询范围内
    auto data_code_vec = ccs->generateDataCubeCodes(point_in_range);
    ASSERT_EQ(data_code_vec.size(), 1);
    const std::string& data_code = data_code_vec[0];

    std::cout << "Data code for point in range: " << data_code << std::endl;

    bool is_covered = (std::find(query_codes.begin(), query_codes.end(), data_code) != query_codes.end());
    EXPECT_TRUE(is_covered) << "The code for a point within the query range should be found in the query results.";
}
