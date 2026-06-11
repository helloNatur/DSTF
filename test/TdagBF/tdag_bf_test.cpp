#include <gtest/gtest.h>
#include "tdag_bf.h"
#include <vector>
#include <string>
#include <bf/bloom_filter/basic.hpp>
#include <random>
#include <iostream>

// 辅助函数：创建模拟关键词（cube code）
std::string create_keyword(int id) {
    return "40." + std::to_string(600 + id) + ",-74.0" + std::to_string(50 + id % 10) + "," + std::to_string(1);
}

class TdagBFTest : public ::testing::Test {
protected:
    void SetUp() override {
        // BF 参数设置（参考 SegmentTreeTest）
        fp_ = 0.01;  
        capacity_ = 10;  // 小容量，便于测试共享
        seed_ = 0;
        height_ = 4;  // 小树：域 [0, 15]
        tdag_bf_ = TdagBF::initialize(height_, fp_, capacity_, seed_);
        ASSERT_TRUE(tdag_bf_ != nullptr);
        max_val_ = (1 << height_) - 1;  // 15

        // 性能/大树：height=6，域 [0, 63]
        perf_height_ = 6;
        perf_tdag_bf_ = TdagBF::initialize(perf_height_, fp_, capacity_, seed_);

        // 模拟关键词
        keywords_ = {create_keyword(1), create_keyword(2), create_keyword(3)};
        double_hashing = true;
        partition = true;
        required_cells = bf::basic_bloom_filter::m(fp_, capacity_);
        optimal_k = bf::basic_bloom_filter::k(required_cells, capacity_);
        // 假设 partition=true（默认）
        if (partition) {
            required_cells += optimal_k - required_cells % optimal_k; // 确保 Bloom Filter 的位向量大小是 k 的倍数
        }
        // 2. 创建全局哈希函数
        global_hasher = bf::make_hasher(optimal_k, seed_, double_hashing);
    }

    double fp_;  // 误判率
    size_t capacity_;  // 预期插入数
    size_t seed_;  // 种子
    int height_;  // 树高度
    std::shared_ptr<TdagBF> tdag_bf_;  // 测试树
    int max_val_;  // 最大值
    int perf_height_;  // 性能树高度
    std::shared_ptr<TdagBF> perf_tdag_bf_;  // 性能树
    std::vector<std::string> keywords_;  // 模拟 cube codes
    bool partition;
    bf::hasher global_hasher; // 全局哈希函数，确保所有bf使用相同的哈希函数
    size_t required_cells;
    size_t optimal_k;
    bool double_hashing;

};

TEST_F(TdagBFTest, ConstructorAndInitialization) {
    // 验证初始化：树高度、范围、BF 非空
    EXPECT_EQ(tdag_bf_->height, height_);
    EXPECT_EQ(tdag_bf_->range, std::make_pair(0, max_val_));
    EXPECT_TRUE(tdag_bf_->bf != nullptr);  // BF 已初始化（空）

    // 验证叶子（递归检查简化：根 left/right）
    if (tdag_bf_->left) {
        EXPECT_EQ(tdag_bf_->left->height, height_ - 1);
        EXPECT_TRUE(tdag_bf_->left->bf != nullptr);
    }
    EXPECT_GE(tdag_bf_->middle.first, -1);  // middle 初始化
}

TEST_F(TdagBFTest, BuildTree) {
    // 验证树构建：中点计算、middle 范围
    int mid = 0 + (max_val_ - 0) / 2;  // 7
    int qtr = (max_val_ - 0) / 4;  // 3
    int expected_mid0 = mid - qtr;  // 4
    int expected_mid1 = mid + qtr + 1;  // 11 (mod 4 >0)
    EXPECT_EQ(tdag_bf_->middle, std::make_pair(expected_mid0, expected_mid1));

    // 验证 BF 指针：初始所有非空，但内容空
    EXPECT_TRUE(tdag_bf_->bf->storage().empty() || tdag_bf_->bf->storage()[0] == 0);  // 初始位 0
    if (tdag_bf_->left) {
        EXPECT_NE(tdag_bf_->bf, tdag_bf_->left->bf);  // 初始不共享
    }
}

TEST_F(TdagBFTest, InsertKeywordAndParentBFReuse) {
    // 插入到叶子，验证向上合并与共享
    int leaf_interval = 5;  // 模拟时间间隔
    std::string keyword1 = keywords_[0];
    tdag_bf_->insert_keyword(leaf_interval, leaf_interval, keyword1);

    // 验证路径 BF 包含关键词（简化：根 BF 应包含）
    EXPECT_TRUE(tdag_bf_->bf->lookup(keyword1));

    // 插入相似关键词（小汉明距离），验证共享
    std::string keyword2 = create_keyword(1);  // 相似，预期汉明小
    int leaf_interval2 = 10;
    tdag_bf_->insert_keyword(leaf_interval2, leaf_interval2, keyword2);

    // 验证父/共享：假设 left/right 共享（汉明 < THRESHOLD）
    if (tdag_bf_->left && tdag_bf_->right) {
        size_t dist = tdag_bf_->hamming_distance(tdag_bf_->left->bf, tdag_bf_->right->bf);
        size_t total_bits = tdag_bf_->left->bf->storage().size() * 8;
        double rel_dist = static_cast<double>(dist) / total_bits;
        EXPECT_LT(rel_dist, TdagBF::THRESHOLD);  // 共享触发
        EXPECT_EQ(tdag_bf_->left->bf, tdag_bf_->right->bf);  // 指针共享
        EXPECT_EQ(tdag_bf_->bf, tdag_bf_->left->bf);  // 父复用
    }

    // lookup 验证
    EXPECT_TRUE(tdag_bf_->bf->lookup(keyword1));
    EXPECT_TRUE(tdag_bf_->bf->lookup(keyword2));
}

TEST_F(TdagBFTest, BFMATCHESPruning) {
    // 插入关键词
    tdag_bf_->insert_keyword(0, 0, keywords_[0]);
    tdag_bf_->insert_keyword(10, 10, keywords_[1]);

    // 匹配测试
    EXPECT_TRUE(tdag_bf_->bf_matches({keywords_[0]}));  // 命中
    EXPECT_TRUE(tdag_bf_->bf_matches({keywords_[1]}));  // 命中
    EXPECT_FALSE(tdag_bf_->bf_matches({"fake_code"}));  // 不命中（忽略 FP）

    // 多关键词：部分命中
    EXPECT_TRUE(tdag_bf_->bf_matches({keywords_[0], "fake"}));
}

TEST_F(TdagBFTest, HammingDistanceMerge) {
    // 直接测试合并：创建两个 BF，汉明小 → 共享；大 → 新 BF
    auto bf1 = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition, optimal_k);
    auto bf2_similar = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition, optimal_k);
    auto bf2_diff = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition, optimal_k);

    // 插入相似项
    bf1->add(keywords_[0]);
    bf2_similar->add(keywords_[0]);  // 相同，汉明=0
    bf2_diff->add("totally_different");  // 不同，汉明大

    // 合并相似：共享
    auto merged_sim = tdag_bf_->merge_bloom(bf1, bf2_similar);
    EXPECT_EQ(merged_sim, bf1);  // 共享 bf1
    EXPECT_TRUE(merged_sim->lookup(keywords_[0]));

    // 合并不同：新 BF
    auto merged_diff = tdag_bf_->merge_bloom(bf1, bf2_diff);
    EXPECT_NE(merged_diff, bf1);  // 新对象
    size_t dist = tdag_bf_->hamming_distance(bf1, bf2_diff);
    EXPECT_GT(dist, 0);  // 非零距离
}

TEST_F(TdagBFTest, IntegrationCoverWithBF) {
    // 端到端：插入到叶子，向上合并，然后 SRC + BF 剪枝
    int test_interval = 5;
    std::string test_keyword = keywords_[0];
    tdag_bf_->insert_keyword(test_interval, test_interval, test_keyword);

    std::pair<int, int> q_range = {4, 6};  // 包含 5
    auto cover = tdag_bf_->get_single_range_cover(q_range, {test_keyword});
    EXPECT_TRUE(tdag_bf_->interval_contains_interval(cover, q_range));  // 覆盖包含查询
    EXPECT_TRUE(tdag_bf_->bf_matches({test_keyword}));  // BF 匹配

    // 测试剪枝：假关键词 → 抛异常（无匹配覆盖）
    EXPECT_THROW(tdag_bf_->get_single_range_cover(q_range, {"fake"}), std::runtime_error);
}

// 在 tdag_bf_test.cpp 文件中

TEST_F(TdagBFTest, GetSingleRangeCoverReturnsMinimalCover) {
    // 目标: 验证函数返回的是最小的有效覆盖，而不是随便一个
    // 树高 height_ = 4, 定义域 [0, 15]
    // 根节点: range=[0, 15], middle=[4, 11]
    // 根的左孩子: range=[0, 7], middle=[2, 5]

    std::string keyword_deep = "deep_keyword"; // 将被插入到树的深处

    // 将关键词插入到叶子节点 3，它被 [2, 5] 和 [0, 7] 和 [0, 15] 覆盖
    tdag_bf_->insert_keyword(3, 3, keyword_deep);
    
    // 查询一个区间 [3, 4]，它也被上述多个区间覆盖
    // 所有可能的、且BF匹配的覆盖区间有：
    // - [2, 5] (节点 [0,7] 的 middle), 长度为 3
    // - [0, 7] (节点 [0,7] 的 range), 长度为 7
    // - [4, 11] (根节点的 middle) - 不包含 [3,4]
    // - [0, 15] (根节点的 range), 长度为 15
    // 正确的、最小的答案应该是 [2, 5]。
    std::pair<int, int> query_range = {3, 4};
    std::vector<std::string> query_keywords = {keyword_deep};
    
    auto cover = tdag_bf_->get_single_range_cover(query_range, query_keywords);
    
    // 断言返回的是我们预期的最小区间
    EXPECT_EQ(cover, std::make_pair(2, 5));
}

TEST_F(TdagBFTest, GetSingleRangeCoverRejectsPartialCandidates) {
    const std::string keyword = "partial_cover_keyword";
    const std::pair<int, int> query_range = {5, 12};

    tdag_bf_->insert_keyword(5, 5, keyword);
    tdag_bf_->insert_keyword(12, 12, keyword);

    auto cover = tdag_bf_->get_single_range_cover(query_range, {keyword});

    EXPECT_TRUE(tdag_bf_->interval_contains_interval(cover, query_range))
        << "cover=[" << cover.first << "," << cover.second << "]";
    EXPECT_EQ(cover, std::make_pair(0, 15));
}

//求解交集if (node && has_match_within(node, q, kws)) return candA; //返回and的情况
TEST_F(TdagBFTest, QueryWindowHasNoKeywordHitInIntersection_ShouldReturnEmpty) {
    // 仅在时间点 7 写入关键词 "kw"
    tdag_bf_->insert_keyword(/*interval_start=*/7, /*interval_end=*/7,
                         std::vector<std::string>{"kw"});

    // 查询窗口 [4,5] 与 kws={"kw"}：交集内没有关键词命中 ⇒ 应当无单覆盖可返回
    // 本实现语义：无解时抛 std::runtime_error
    auto cover = tdag_bf_->get_single_range_cover({4,5}, {"kw"});
    EXPECT_EQ(cover, std::make_pair(-1, -1));
}

// //求解并集if (!node->bf_matches(kws)) return; // 返回or的情况
// TEST_F(TdagBFTest, QueryWindowHasNoKeywordHitInUnion) {
//     // 仅在时间点 7 写入关键词 "kw"
//     tdag_bf_->insert_keyword(/*interval_start=*/7, /*interval_end=*/7,
//                          std::vector<std::string>{"kw"});

//     // 查询窗口 [4,5] 与 kws={"kw"}：并集
    
//     auto cover = tdag_bf_->get_single_range_cover({4,5}, {"kw"});
//     EXPECT_EQ(cover, std::make_pair(4, 7));
// }

// 把一组区间做归一化：排序 + 去重，便于“忽略顺序”的比较
static std::vector<std::pair<int,int>>
NormalizeIntervals(std::vector<std::pair<int,int>> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// 校验“所有区间都覆盖某个叶子”
static void ExpectAllCoverLeaf(const std::vector<std::pair<int,int>>& v, int leaf) {
    for (auto [L,R] : v) {
        EXPECT_LE(L, R);
        EXPECT_LE(L, leaf);
        EXPECT_GE(R, leaf);
    }
}


TEST_F(TdagBFTest, CoveringIntervals_EqualsDescendTree_Leaf5) {
    // 预设：fixture 里 height_=4, 定义域 [0,15]
    // 把一组区间做归一化：排序 + 去重，便于“忽略顺序”的比较

    const int leaf = 5;

    // 1) 新函数返回的“覆盖链”
    auto got = tdag_bf_->covering_intervals_for_leaf(leaf);

    // 2) 作为“参考”的 descend_tree（同一根域）
    auto ref = tdag_bf_->descend_tree(leaf, tdag_bf_->range);

    // 3) 忽略顺序比较（都做排序+去重）
    EXPECT_EQ(NormalizeIntervals(got), NormalizeIntervals(ref));

    // 4) 每个区间都应覆盖该叶
    ExpectAllCoverLeaf(got, leaf);

    // 5) 链首应该包含根区间、链尾应该是 [leaf,leaf]（如果你实现里有这个语义）
    ASSERT_FALSE(got.empty());
    EXPECT_EQ(got.back(), std::make_pair(leaf, leaf));
    // 注：若你的实现把 [leaf,leaf] 放在中间，这条可以改为在集合里“能找到”
    // bool has_point=false; for(auto &p:got) if(p==std::make_pair(leaf,leaf)) has_point=true; EXPECT_TRUE(has_point);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
