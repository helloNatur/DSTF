#include <gtest/gtest.h>
#include "cube_code.h"
#include <memory>
#include <algorithm>

class CubeCodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<double>& min_vals = {40.5, -74.2}; // 纬度最小值
        const std::vector<double>& max_vals = {41.0, -73.7}; // 纬度最大值
        // 初始化 2D CubeCode，纬度 [40.5, 41]，经度 [-74.2, -73.7],3 级
        ccs = std::make_shared<CubeCode>(2, min_vals, max_vals,3);
    }

    std::shared_ptr<CubeCode> ccs;
};

TEST_F(CubeCodeTest, GenerateDataCubeCodes) {
    // 测试点：(40.65, -74.2)
    std::vector<double> point = {40.65, -74.2};
    auto codes = ccs->generateDataCubeCodes(point);
    ASSERT_EQ(codes.size(), 3); // 级别数 k=3，应生成 3 个编码

    // 打印 codes 内容
    std::cout << "Generated codes: ";
    for (const auto& code : codes) {
        std::cout << code << " ";
    }
    std::cout << std::endl;
    // 手动计算预期值（近似值）
    EXPECT_EQ(codes[0], "40.625000,-74.075000,1");
    EXPECT_EQ(codes[1], "40.687500,-74.137500,2");
    EXPECT_EQ(codes[2], "40.656250,-74.168750,3");
}


TEST_F(CubeCodeTest, GenerateQueryCubeCodes) {
    // 改成完整覆盖 Level 1 的 cube C0
    std::vector<double> query_min = {40.5, -74.2};
    std::vector<double> query_max = {40.75, -73.95};
    // std::vector<double> query_min = {40.6, -74.2};
    // std::vector<double> query_max = {40.7, -74.0};
    auto codes = ccs->generateQueryCubeCodes(query_min, query_max);
    ASSERT_GE(codes.size(), 1); // 至少有一个编码

    // 打印 codes 内容
    std::cout << "Generated codes: ";
    for (const auto& code : codes) {
        std::cout << code << " ";
    }
    std::cout << std::endl;

    // 验证是否包含预期编码（级别 1 中心）
    // EXPECT_TRUE(std::find(codes.begin(), codes.end(), "40.625,-74.075,1") != codes.end());

}

// ===== 新增测试用例开始 =====
// 使用独立的 TEST 宏，而不是 TEST_F，以避免使用固定的 level=3 的 fixture
// 这样我们可以自由地为这个测试用例设置 levels=4
TEST(CubeCodeStandaloneTest, GenerateCodesWithLevel4) {
    // 1. 定义与上面测试一致的全局范围和新的 levels
    const std::vector<double> min_vals = {40.5, -74.2};
    const std::vector<double> max_vals = {41.0, -73.7};
    const int levels = 4;
    auto ccs_level4 = std::make_shared<CubeCode>(2, min_vals, max_vals, levels);

    // 2. 测试数据点生成 (generateDataCubeCodes)
    std::vector<double> point = {40.71, -73.99};
    auto data_codes = ccs_level4->generateDataCubeCodes(point);
    ASSERT_EQ(data_codes.size(), levels) << "For levels=4, generateDataCubeCodes should return 4 codes.";
    
    std::cout << "Generated data codes for Level 4: ";
    for (const auto& code : data_codes) {
        std::cout << "\"" << code << "\" ";
    }
    std::cout << std::endl;

    // 3. 测试范围查询 (generateQueryCubeCodes) 并验证其正确性
    std::vector<double> query_min = {40.7, -74.0};
    std::vector<double> query_max = {40.76, -73.98}; // 这个范围包含上面的测试点
    auto query_codes = ccs_level4->generateQueryCubeCodes(query_min, query_max);
    ASSERT_FALSE(query_codes.empty()) << "Query should generate at least one cube code for a valid range.";

    std::cout << "Generated query codes for Level 4: ";
    for (const auto& code : query_codes) {
        std::cout << "\"" << code << "\" ";
    }
    std::cout << std::endl;

    // 4. 核心验证：检查点编码是否被查询编码所覆盖
    // 这是解决“假阴性”问题的关键。
    // 逻辑：数据点的编码路径（从根到叶）中，至少有一个编码，其前缀是查询编码集中的一个元素。
    // 简化检查：数据点的编码路径中，至少有一个编码，与查询编码集中的一个编码完全相同。
    bool is_covered = false;
    for (const auto& d_code : data_codes) {
        for (const auto& q_code : query_codes) {
            // 如果查询编码是数据点层级编码的前缀，则说明点被覆盖
            if (d_code.rfind(q_code, 0) == 0) {
                is_covered = true;
                break;
            }
        }
        if (is_covered) {
            break;
        }
    }

    ASSERT_TRUE(is_covered) << "The codes generated for the query range MUST cover the point inside it.";
}
// ===== 新增测试用例结束 =====


TEST_F(CubeCodeTest, InvalidPointDimensions) {
    std::vector<double> invalid_point = {40.65,}; // 维度不足
    EXPECT_THROW(ccs->generateDataCubeCodes(invalid_point), std::invalid_argument);
}

TEST_F(CubeCodeTest, InvalidQueryDimensions) {
    std::vector<double> query_min = {40.6,};
    std::vector<double> query_max = {40.7, -74.01};
    EXPECT_THROW(ccs->generateQueryCubeCodes(query_min, query_max), std::invalid_argument);
}

TEST_F(CubeCodeTest, BoundaryPoint) {
    std::vector<double> point = {40.5, -74.2}; // 边界点
    auto codes = ccs->generateDataCubeCodes(point);
    ASSERT_EQ(codes.size(), 1);
    EXPECT_EQ(codes[0], "40.625000,-74.075000,1"); // 级别 1 中心点
    // EXPECT_EQ(codes[0], "40.5312,-74.1688,3");
}



int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}