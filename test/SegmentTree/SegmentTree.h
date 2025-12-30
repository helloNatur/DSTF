#ifndef SEGMENT_TREE_H
#define SEGMENT_TREE_H

#include <vector>
#include <string>
#include <functional>
#include <bf/bloom_filter/basic.hpp>
#include <memory>
// #include <unordered_set>

class SegmentTree {
private:
    struct Node {
        int left, right;
        std::shared_ptr<bf::basic_bloom_filter> bf;
        // std::vector<std::vector<unsigned char>> tokens;
        std::vector<std::shared_ptr<std::vector<unsigned char>>> tokens;//共享指针

        // 默认构造函数
        Node() : left(0), right(0), bf(nullptr) {}

        //参数构造函数，不初始化bf
        Node(int l, int r)
            : left(l), right(r), bf(nullptr) {}

        // 立即初始化bf
        // Node(int l, int r, double fp, size_t capacity, size_t seed = 0, 
        //     bool double_hashing = true, bool partition = true)
        //     : left(l), right(r), bf(std::make_shared<bf::basic_bloom_filter>(fp, capacity, seed, double_hashing, partition)) {}
           
        
        //拷贝构造函数(深拷贝)
        Node(const Node& other) noexcept
            : left(other.left), right(other.right), tokens(other.tokens) {
                bf = other.bf ? std::make_shared<bf::basic_bloom_filter>(*other.bf) : nullptr;
            }

        //移动构造函数
        Node(Node&& other) noexcept
            : left(other.left), right(other.right), bf(std::move(other.bf)), tokens(std::move(other.tokens)) {
            other.left = 0;
            other.right = 0;
        }

        //拷贝赋值运算符，自我赋值保护
        Node& operator=(const Node& other) noexcept {
            if (this != &other) {
                left = other.left;
                right = other.right;
                bf = other.bf ? std::make_shared<bf::basic_bloom_filter>(*other.bf) : nullptr;
                tokens = other.tokens;
            }
            return *this;
        }

        //移动赋值运算符
        Node& operator=(Node&& other) noexcept {
            if (this != &other) {
                left = other.left;
                right = other.right;
                bf = std::move(other.bf); //移动bf和tokens的所有权
                tokens = std::move(other.tokens);
                other.left = 0;
                other.right = 0;
            }
            return *this;
        }
    };

    std::vector<Node> tree;
    int n;
    double fp;
    size_t capacity;
    size_t seed;
    bool double_hashing;
    bool partition;
    bf::hasher global_hasher; // 全局哈希函数，确保所有bf使用相同的哈希函数
    size_t required_cells;
    size_t optimal_k;

    // double threshold = 0.001;  // 新增：合并阈值
    // size_t num_unique_bfs = 0;  // 新增：唯一 BF 数量
    // size_t total_bits = 0;      // 新增：总 bit 数
    // std::unordered_set<const bf::basic_bloom_filter*> unique_bf_ptrs;  // 新增：唯一 BF 指针 set（用于去重）

public:
    struct IntervalResult {
        int left, right;
        long long day_ts;//关于具体某天的信息
        std::vector<std::shared_ptr<std::vector<unsigned char>>> tokens;

        //原构造函数：补上day_ts默认为0
        IntervalResult(int l, int r, 
                    const std::vector<std::shared_ptr<std::vector<unsigned char>>>& t)
            : left(l), right(r), day_ts(0),tokens(t) {}

        //新增：带day_ts的构造函数
        IntervalResult(int l,int r,long long day,
                    const std::vector<std::shared_ptr<std::vector<unsigned char>>>& t)
            : left(l), right(r),day_ts(day),tokens(t){}
    };

protected:
    void build(size_t node, int start, int end);
    void update(size_t node, int start, int end, int id, 
                const std::shared_ptr<std::vector<unsigned char>>& token, 
                const std::vector<std::string>& keywords);
    void query(size_t node, int start, int end, int l, int r, 
                std::vector<std::shared_ptr<std::vector<unsigned char>>>& result);
    void getCandidateIntervals(size_t node, int start, int end, 
                              int l, int r, const std::vector<std::string>& keywords,
                              std::vector<IntervalResult>& result);
    void updateParent(size_t node);
    void mergeLeafNodes(size_t node,int start,int end);
    static constexpr double THRESHOLD = 0.001; // hamming 距离阈值
    // static constexpr double THRESHOLD = 0.001; // 临时禁用合并
    size_t hamming_distance(const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
                            const std::shared_ptr<bf::basic_bloom_filter>& bf2);
    

public:
    SegmentTree(int size, double fp, size_t capacity, 
                size_t seed = 0, bool double_hashing = true, bool partition = true);
    void update(int id, const std::shared_ptr<std::vector<unsigned char>>& token, const std::vector<std::string>& keywords);
    std::vector<std::shared_ptr<std::vector<unsigned char>>> query(int l, int r);
    std::vector<IntervalResult> getCandidateIntervals(int l, int r, const std::vector<std::string>& keywords);
    std::shared_ptr<bf::basic_bloom_filter> merge_bloom(
                const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
                const std::shared_ptr<bf::basic_bloom_filter>& bf2);
    

    int get_size() const { return n; }
    const std::vector<Node>& get_tree() const { return tree; }
    // const bf::hasher& get_global_hasher() const { return global_hasher; }
    // size_t get_required_cells() const { return required_cells; }
    // size_t get_optimal_k() const { return optimal_k; }
};

#endif