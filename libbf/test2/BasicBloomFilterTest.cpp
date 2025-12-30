#include <gtest/gtest.h>
#include <bf/bloom_filter/basic.hpp>
#include <bf/wrap.hpp>
#include <stdexcept>



// 测试Bloom Filter的构造函数
TEST(BasicBloomFilterTest, Constructor) {
    // 测试使用fp率和容量构造
    EXPECT_NO_THROW({
        bf::basic_bloom_filter filter(0.01, 100);
    });
    
    // 测试使用hasher和cells构造
    EXPECT_NO_THROW({
        auto h = bf::make_hasher(3);
        bf::basic_bloom_filter filter(h, 1000);
    });
}

// 测试基本的添加和查找功能
TEST(BasicBloomFilterTest, AddAndLookup) {
    bf::basic_bloom_filter filter(0.01, 100);
    
    // 添加元素
    filter.add("test1");
    filter.add("test2");
    
    // 验证已添加元素的查找
    EXPECT_EQ(filter.lookup("test1"), 1u);
    EXPECT_EQ(filter.lookup("test2"), 1u);
    
    // 验证未添加元素的查找
    EXPECT_EQ(filter.lookup("test3"), 0u);
}

// 测试清除功能
TEST(BasicBloomFilterTest, Clear) {
    bf::basic_bloom_filter filter(0.01, 100);
    
    // 添加元素
    filter.add("test1");
    EXPECT_EQ(filter.lookup("test1"), 1u);
    
    // 清除后验证
    filter.clear();
    EXPECT_EQ(filter.lookup("test1"), 0u);
}

// 测试移除功能
TEST(BasicBloomFilterTest, Remove) {
    bf::basic_bloom_filter filter(0.01, 100);
    
    // 添加并移除元素
    filter.add("test1");
    EXPECT_EQ(filter.lookup("test1"), 1u);
    
    filter.remove(bf::wrap("test1"));
    EXPECT_EQ(filter.lookup("test1"), 0u);
}

// 测试交换功能
TEST(BasicBloomFilterTest, Swap) {
    bf::basic_bloom_filter filter1(0.01, 100);
    bf::basic_bloom_filter filter2(0.01, 100);
    
    // 在filter1中添加元素
    filter1.add("test1");
    
    // 交换后验证
    filter1.swap(filter2);
    EXPECT_EQ(filter2.lookup("test1"), 1u);
    EXPECT_EQ(filter1.lookup("test1"), 0u);
}

// 测试不同类型数据的添加和查找
TEST(BasicBloomFilterTest, DifferentTypes) {
    bf::basic_bloom_filter filter(0.01, 100);
    
    // 测试字符串
    filter.add(std::string("test"));
    EXPECT_EQ(filter.lookup(std::string("test")), 1u);
    
    // 测试整数
    filter.add(42);
    EXPECT_EQ(filter.lookup(42), 1u);
    
    // 测试浮点数
    filter.add(3.14);
    EXPECT_EQ(filter.lookup(3.14), 1u);
}

// 测试参数边界条件
TEST(BasicBloomFilterTest, EdgeCases) {
    // 测试极小的fp率
    EXPECT_NO_THROW({
        bf::basic_bloom_filter filter(0.0001, 100);
    });
    
    // 测试极大的容量
    EXPECT_NO_THROW({
        bf::basic_bloom_filter filter(0.01, 1000000);
    });
    

    // 测试极限情况下的fp率
    // 使用接近0但不等于0的值
    EXPECT_NO_THROW({
      bf::basic_bloom_filter filter(std::numeric_limits<double>::min(), 100);
    });
    
    // 使用接近1但不等于1的值
    EXPECT_NO_THROW({
        bf::basic_bloom_filter filter(0.9999, 100);
    });

    // // 测试边界fp率
    // EXPECT_THROW({
    //     bf::basic_bloom_filter filter(0.0, 100);
    // }, std::invalid_argument);
    
    // EXPECT_THROW({
    //     bf::basic_bloom_filter filter(1.0, 100);
    // }, std::invalid_argument);
}

// 测试哈希函数的正确性
TEST(BasicBloomFilterTest, HashFunctionality) {
    // 使用指定数量的哈希函数
    auto h = bf::make_hasher(5);
    bf::basic_bloom_filter filter(h, 1000);
    
    std::string test_str = "test";
    filter.add(test_str);
    
    // 验证查找结果
    EXPECT_EQ(filter.lookup(test_str), 1u);
}


TEST(BasicBloomFilterTest,bloom_filter_basic) { //没过
    bf::basic_bloom_filter bf(0.8, 10);
    bf.add("foo");
    bf.add("bar");
    bf.add("baz");
    bf.add('c');
    bf.add(4.2);
    bf.add(4711ULL);
    // True-positives
    EXPECT_EQ(bf.lookup("foo"), 1u);
    EXPECT_EQ(bf.lookup("bar"), 1u);
    EXPECT_EQ(bf.lookup("baz"), 1u);
    EXPECT_EQ(bf.lookup(4.2), 1u);
    EXPECT_EQ(bf.lookup('c'), 1u);
    EXPECT_EQ(bf.lookup(4711ULL), 1u);
    // True-negatives
    EXPECT_EQ(bf.lookup("qux"), 0u);
    EXPECT_EQ(bf.lookup("graunt"), 0u);
    EXPECT_EQ(bf.lookup(3.1415), 0u);
    // False-positives
    EXPECT_EQ(bf.lookup("corge"), 1u);
    EXPECT_EQ(bf.lookup('a'), 1u);

    // another filter
    bf::basic_bloom_filter obf(0.8, 10);
    obf.swap(bf);

    EXPECT_EQ(obf.lookup("foo"), 1u);

    // Make bf using another filter's storage
    bf::hasher h = obf.hasher_function();
    bf::bitvector b = obf.storage();
    bf::basic_bloom_filter obfc(h, b);
    EXPECT_EQ(obfc.storage(), b);
    EXPECT_EQ(obfc.lookup("foo"), 1u);
}
