#include <gtest/gtest.h>
#include "SegmentTree.h"
#include <vector>
#include <string>
#include <bf/bloom_filter/basic.hpp>

std::shared_ptr<std::vector<unsigned char>> create_token(const std::string& str) {
        return std::make_shared<std::vector<unsigned char>>(str.begin(), str.end());
    }

    class SegmentTreeTest : public ::testing::Test {
    protected:
        void SetUp() override {
            fp = 0.01;  
            capacity = 10;  
            seed = 0;
            double_hashing = true;
            partition = true;
            size = 8;
            required_cells = bf::basic_bloom_filter::m(fp,capacity);
            optimal_k = bf::basic_bloom_filter::k(required_cells, capacity);
            if(partition){
                required_cells += optimal_k - required_cells % optimal_k; // 确保 Bloom Filter 的位向量大小是 k 的倍数
            }
            global_hasher = bf::make_hasher(optimal_k, seed, double_hashing);
            
            segment_tree = std::make_shared<SegmentTree>(size, fp, capacity, seed, double_hashing, partition);
        }

        int size;  // SegmentTree 的大小 0-7
        double fp; // 误判率
        size_t capacity; // 希望插入的最大元素数量
        size_t seed; // 随机种子
        bool double_hashing; // 是否使用双哈希
        bool partition; // 是否分区存储 Bloom Filter
        std::shared_ptr<SegmentTree> segment_tree;
        bf::hasher global_hasher; // 全局哈希函数，确保所有 Bloom Filter 使用相同的哈希函数
        size_t required_cells;  // 位图大小
        size_t optimal_k; // 最优哈希函数个数

    };

    TEST_F(SegmentTreeTest, ConstructorAndInitialization) {
        EXPECT_EQ(segment_tree->get_size(), size);
    EXPECT_GE(segment_tree->get_tree().size(), 4 * size);
}

TEST_F(SegmentTreeTest, BuildTree) {
    const auto& tree = segment_tree->get_tree();
    EXPECT_EQ(tree[1].left, 0);
    EXPECT_EQ(tree[1].right, size - 1);
    
    for (int i = 0; i < size; ++i) {
        int node = 1;
        int start = 0, end = size - 1;
        while (start != end) {
            int mid = (start + end) / 2;
            if (i <= mid) {
                node = 2 * node;
                end = mid;
            } else {
                node = 2 * node + 1;
                start = mid + 1;
            }
        }
        EXPECT_EQ(tree[node].left, i);
        EXPECT_EQ(tree[node].right, i);
        EXPECT_TRUE(tree[node].tokens.empty());
        ASSERT_TRUE(tree[node].bf != nullptr);
        EXPECT_EQ(tree[node].bf->lookup("test"), 0);
    }
}

TEST_F(SegmentTreeTest, BasicBloomFilterFunctionality) {

    auto bf = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition,optimal_k);
    std::string keyword = "test";
    bf->add(keyword);
    EXPECT_TRUE(bf->lookup(keyword)) << "Bloom filter should contain added keyword";
}

//验证merge_bloom正确
TEST_F(SegmentTreeTest, MergeBloomFunction) {
    // 创建两个布隆过滤器

    auto bf1 = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition,optimal_k);
    auto bf2 = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition,optimal_k);

    // auto bf1 = std::make_shared<bf::basic_bloom_filter>(global_hasher, bf::basic_bloom_filter::m(fp, capacity));
    // auto bf2 = std::make_shared<bf::basic_bloom_filter>(global_hasher, bf::basic_bloom_filter::m(fp, capacity));

    // auto bf1 = std::make_shared<bf::basic_bloom_filter>(fp, capacity, seed, double_hashing, partition);
    // auto bf2 = std::make_shared<bf::basic_bloom_filter>(fp, capacity, seed, double_hashing, partition);

    // 向 bf1 和 bf2 插入不同的 keyword
    std::string keyword1 = "keyword0table1_keyword_0_0";
    std::string keyword2 = "keyword0table1_keyword_1_0";
    bf1->add(keyword1);
    bf2->add(keyword2);

    EXPECT_EQ(bf1->storage().size(), bf2->storage().size()); // 位图大小一致
    std::cerr << "bf1.size:"<<bf1->storage().size()<< std::endl;
    std::cerr << "bf2.size:"<<bf2->storage().size()<< std::endl;
    // EXPECT_EQ(bf1, bf2); // 指针相等（共享同一个对象）

    // 验证初始状态
    EXPECT_TRUE(bf1->lookup(keyword1));
    EXPECT_FALSE(bf1->lookup(keyword2));
    EXPECT_TRUE(bf2->lookup(keyword2));
    EXPECT_FALSE(bf2->lookup(keyword1));

    // 合并布隆过滤器
    auto merged_bf = segment_tree->merge_bloom(bf1, bf2);  //返回的bf1，bf1里有kw1和kw2

    // 验证合并后的布隆过滤器
    ASSERT_TRUE(merged_bf != nullptr);
    EXPECT_TRUE(merged_bf->lookup(keyword1));
    EXPECT_TRUE(merged_bf->lookup(keyword2));

    // 验证位向量大小
    EXPECT_EQ(merged_bf->storage().size(), bf1->storage().size());
    

    // 验证哈希函数一致性
    // EXPECT_EQ(merged_bf->hasher_function(), bf1->hasher_function()) << "Merged Bloom Filter should use same hasher";

    // 测试边界情况：一个布隆过滤器为空
    auto empty_bf = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition,optimal_k);
    auto merged_with_empty = segment_tree->merge_bloom(bf1, empty_bf);
    ASSERT_TRUE(merged_with_empty != nullptr);
    EXPECT_TRUE(merged_with_empty->lookup(keyword1)) ;
    EXPECT_TRUE(merged_with_empty->lookup(keyword2)) ;

    // 测试两个空布隆过滤器
    auto empty_bf2 = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition,optimal_k);
    auto merged_empty = segment_tree->merge_bloom(empty_bf, empty_bf2);
    ASSERT_TRUE(merged_empty != nullptr);
    EXPECT_FALSE(merged_empty->lookup(keyword1)) ;
}

TEST_F(SegmentTreeTest, UpdateSingleTokenAndKeyword) {
    auto token = create_token("token1");
    std::string keyword = "keyword1";
    int id = 5;
    segment_tree->update(id, token, keyword);

    auto result = segment_tree->query(id, id);
    ASSERT_EQ(result.size(), 1);//0
    EXPECT_EQ(result[0], token);

    auto intervals = segment_tree->getCandidateIntervals(id, id, keyword);
    ASSERT_EQ(intervals.size(), 1);
    EXPECT_EQ(intervals[0].left, id);
    EXPECT_EQ(intervals[0].right, id);
    ASSERT_EQ(intervals[0].tokens.size(), 1);
    EXPECT_EQ(intervals[0].tokens[0], token);

    result = segment_tree->query(id + 1, id + 1);
    EXPECT_TRUE(result.empty());
}

TEST_F(SegmentTreeTest, BloomFilterORAggregation) {
    auto token1 = create_token("token1");
    auto token2 = create_token("token2");
    std::string keyword = "keyword";
    segment_tree->update(2, token1, keyword);
    segment_tree->update(3, token2, keyword);

    const auto& tree = segment_tree->get_tree();
    EXPECT_TRUE(tree[1].bf->lookup(keyword));

    
    auto merged_bf = segment_tree->merge_bloom(tree[10].bf, tree[11].bf);
    EXPECT_EQ(tree[1].bf->lookup(keyword), merged_bf->lookup(keyword));
}

TEST_F(SegmentTreeTest, MergeSimilarBloomFilters) {
    auto token1 = create_token("token1");
    auto token2 = create_token("token2");
    std::string keyword1 = "keyword1";
    std::string keyword2 = "keyword1"; // 假设相似关键字
    segment_tree->update(2, token1, keyword1);
    segment_tree->update(3, token2, keyword2);

    const auto& tree = segment_tree->get_tree();
    int node2 = 1, start = 0, end = size - 1; //node2 索引为10
    while (start != end) {
        int mid = (start + end) / 2;
        if (2 <= mid) {
            node2 = 2 * node2;
            end = mid;
        } else {
            node2 = 2 * node2 + 1;
            start = mid + 1;
        }
    }
    int node3 = 1, start2 = 0, end2 = size - 1;//node3索引为11，node2和3属于同一个父节点
    while (start2 != end2) {
        int mid = (start2 + end2) / 2;
        if (3 <= mid) {
            node3 = 2 * node3;
            end2 = mid;
        } else {
            node3 = 2 * node3 + 1;
            start2 = mid + 1;
        }
    }
    // 验证是否共享布隆过滤器
    EXPECT_EQ(tree[node2].bf, tree[node3].bf); // 指针相同
    EXPECT_TRUE(tree[node2].bf->lookup(keyword1));
    EXPECT_TRUE(tree[node2].bf->lookup(keyword2));

    auto result = segment_tree->query(2, 3);
    ASSERT_EQ(result.size(), 2);
    EXPECT_TRUE(std::find(result.begin(), result.end(), token1) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), token2) != result.end());
}

TEST_F(SegmentTreeTest, CoarseIntervals) {
    auto token1 = create_token("token1");
    auto token2 = create_token("token2");
    std::string keyword = "keyword";
    segment_tree->update(2, token1, keyword);
    segment_tree->update(4, token2, keyword);

    auto intervals = segment_tree->getCandidateIntervals(0, size - 1, keyword);
    ASSERT_GE(intervals.size(), 2);
    bool found_leaf_2 = false, found_leaf_4 = false;
    // bool found_coarse = false;
    for (const auto& interval : intervals) {
        if (interval.left == 2 && interval.right == 2) {
            found_leaf_2 = true;
            ASSERT_EQ(interval.tokens.size(), 1);
            EXPECT_EQ(interval.tokens[0], token1);
        } else if (interval.left == 4 && interval.right == 4) {
            found_leaf_4 = true;
            ASSERT_EQ(interval.tokens.size(), 1);
            EXPECT_EQ(interval.tokens[0], token2);
        }
        // } else if (interval.left < 2 && interval.right > 4) {
        //     found_coarse = true; // 找到粗略区间
        // }
    }
    // EXPECT_TRUE(found_leaf_2 && found_leaf_4 && found_coarse);
    EXPECT_TRUE(found_leaf_2 && found_leaf_4 );
}

TEST_F(SegmentTreeTest,ParentBloomFilterReuse){
    auto token1 = create_token("token1");
    auto token2 = create_token("token2");
    std::string keyword1 = "keyword1";
    std::string keyword2 = "keyword1";
    segment_tree->update(4,token1,keyword1);
    segment_tree->update(5,token2,keyword2);

    const auto& tree = segment_tree->get_tree();
    int node4 = 12,node5 = 13;
    int parent_node = 6;
    EXPECT_EQ(tree[node4].bf,tree[node5].bf);//叶子结点共享
    EXPECT_EQ(tree[node4].bf,tree[parent_node].bf);//父节点复用叶子结点
    EXPECT_TRUE(tree[parent_node].bf->lookup(keyword1));//叶子结点共享
    EXPECT_TRUE(tree[parent_node].bf->lookup(keyword2));//叶子结点共享
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}