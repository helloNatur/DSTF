#include "SegmentTree.h"
#include <algorithm>
#include <iostream>
#include <limits>

// TODO：
// 1:用or聚合方式维护内部节点的bloom filter
// 2:在插入时不再为每个节点都创建新的bloom filter，只对叶子结点做add
// 3:减少bf数量：相似度高的叶子节点进行or操作，并和父节点共享一个bf，如用如下策略判断合并：
// if (hamming_distance(bf1, bf2) < THRESHOLD) {
//     merge_bf(bf1, bf2); // 共享 bf 对象
// }
// 这将把原来的 O(n) 个 Bloom Filter 减少为 O(k)，k 远小于 n。


SegmentTree::SegmentTree(int size, double fp, size_t capacity, size_t seed,
                        bool double_hashing, bool partition) 
    : n(size),fp(fp),capacity(capacity),seed(seed),double_hashing(double_hashing),partition(partition) {
    // 1. 先计算布隆过滤器参数并赋值给成员变量
    required_cells = bf::basic_bloom_filter::m(fp,capacity);
    optimal_k = bf::basic_bloom_filter::k(required_cells, capacity);
    if(partition){
        required_cells += optimal_k - required_cells % optimal_k; // 确保 Bloom Filter 的位向量大小是 k 的倍数
    }

    // 2. 创建全局哈希函数
    global_hasher = bf::make_hasher(optimal_k, seed, double_hashing);

    // 3. 初始化树的大小和结构
    tree.resize(4 * size,Node()); // 依赖默认构造函数
    build(1, 0, size - 1);
    
}

// 计算两个 Bloom Filter 之间的 Hamming 距离
size_t SegmentTree::hamming_distance(const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
                                      const std::shared_ptr<bf::basic_bloom_filter>& bf2) {
    if (!bf1 || !bf2) return std::numeric_limits<size_t>::max(); // 如果任一 Bloom Filter 为空，返回最大距离
    auto bits1 = bf1->storage();
    auto bits2 = bf2->storage();
    size_t distance = 0;
    for (size_t i = 0; i < bits1.size(); ++i) {
        distance += __builtin_popcount(bits1[i] ^ bits2[i]); // 使用位异或计算 Hamming 距离
    }
    return distance;
}


//TODO：
//输入：bfA和bfB
//输出：A和B的进行or运算后父节点
std::shared_ptr<bf::basic_bloom_filter> SegmentTree::merge_bloom(
    const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
    const std::shared_ptr<bf::basic_bloom_filter>& bf2) {
    //处理空指针的情况
    if(!bf1 && !bf2) {
        std::cerr << "Error: Both Bloom Filters are null" << std::endl;
        return nullptr; // 两个都为空，返回空指针
    }
    if (!bf1) return bf2;
    if (!bf2) return bf1;

    // 确保两个 Bloom Filter 的位向量大小相同
    const auto& bits1 = bf1->storage();
    const auto& bits2 = bf2->storage();
    if (bits1.size() != bits2.size()) {
        std::cerr << "Error: Bloom filter bit vectors have different sizes " <<"bits1.size:"<<bits1.size()<< std::endl;
        std::cerr << "Error: Bloom filter bit vectors have different sizes " <<"bits2.size:"<<bits2.size()<< std::endl;
        return bf1;
    }

    // bf::bitvector merged_bits = bits1; // 复制 bf1 的位向量
    // for (size_t i = 0; i < merged_bits.size(); ++i) {
    //     merged_bits[i] |= bits2[i]; // 位或操作
    // }

    bf::bitvector merged_bit = bits1 | bits2;
    // 使用 bf1 的哈希函数创建新的布隆过滤器
    // auto merged_bf = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition,optimal_k);
    // merged_bf->storage() = merged_bits; // 设置合并后的位向量
    bf1->set_storage(std::move(merged_bit)); 
    
    return bf1;
}

void SegmentTree::build(size_t node, int start, int end) {
    tree[node] = Node{start, end};
    if (start == end) {
        tree[node].bf = std::make_shared<bf::basic_bloom_filter>
        (global_hasher, 
         required_cells,
         partition,
         optimal_k); // 初始化叶子节点的 Bloom Filter
        return;
    }
    int mid = (start + end) / 2;
    build(2 * node, start, mid);
    build(2 * node + 1, mid + 1, end);
    //初始构建时，父节点的bf通过子节点聚合
    //父节点暂时不分配bf，延迟到updateParent
    // updateParent(node);
}

void SegmentTree::updateParent(size_t node) {
    if (node <= 0 || node >= tree.size()||tree[node].left == tree[node].right) {
        std::cerr << "Error: Node index out of bounds or leaf node" << std::endl;
        return;
    }
    size_t left_child = 2 * node;
    size_t right_child = 2 * node + 1;

    if(left_child>=tree.size()||right_child>=tree.size()){
        return;
    }

    //如果子节点共享同一个bf，父节点直接复用
    if (tree[left_child].bf==tree[right_child].bf && tree[left_child].bf) {
        // 合并子节点的 Bloom Filter
        tree[node].bf = tree[left_child].bf;
    } else {
        tree[node].bf = merge_bloom(tree[left_child].bf,tree[right_child].bf);
    }
}

void SegmentTree::mergeLeafNodes(size_t node, int start, int end) {
    if (start != end) {
        // 仅对叶子结点处理
        return;
    }

    size_t parent_node = node / 2;
    if(parent_node < 1 || parent_node >= tree.size()) {
        std::cerr << "Error: Parent node index out of bounds" << std::endl;
        return;
    }

    
    // 找到当前叶子节点的相邻兄弟节点，以便比较它们的布隆过滤器。
    int sibling = (node % 2 == 0) ? node + 1 : node - 1;
    if(static_cast<size_t>(sibling)>= tree.size() || !tree[sibling].bf || !tree[node].bf) {
        std::cerr << "Error: Sibling node index out of bounds" << std::endl;
        return;
    }


    // 排除两个bf为空的情况
    if(!tree[node].bf || !tree[sibling].bf || tree[node].bf->storage().count()==0 
    ||tree[sibling].bf->storage().count()==0){
        return;
    }

    size_t dist = hamming_distance(tree[node].bf, tree[sibling].bf);
    size_t bf_size = tree[node].bf->storage().size(); //直接使用 bitvector 的总位数
    // size_t bf_size = bf::basic_bloom_filter::m(fp, capacity);
    // if (partition_) {
    //     size_t optimal_k = bf::basic_bloom_filter::k(bf_size, capacity);
    //     bf_size += optimal_k - bf_size % optimal_k;
    // }
    if (dist < THRESHOLD * bf_size) {
        // 如果相似度高，合并 Bloom Filter
        //合并两个布隆过滤器，两个叶子结点和父节点共享一个
        tree[node].bf = merge_bloom(tree[node].bf,tree[sibling].bf);
        tree[sibling].bf = tree[node].bf; // 共享同一布隆过滤器
        tree[parent_node].bf = tree[node].bf;//父节点直接复用
        updateParent(parent_node/2); //递归更新上层父节点
    }

}

// 每个节点有一个布隆过滤器 bf，用于存储与该节点区间相关的所有 keyword。
// 每个节点有一个 tokens 向量，存储与该节点区间相关的 token 列表。
// 叶子节点的 tokens 存储对应索引的单个 token，而非叶子节点的 tokens 通常为空（除非在某些特殊情况下存储聚合数据）。
void SegmentTree::update(size_t node, int start, int end, int id, 
                        const std::shared_ptr<std::vector<unsigned char>>& token, 
                        const std::string& keyword) {
    if (start == end) {
        if (!tree[node].bf) {
            tree[node].bf = std::make_shared<bf::basic_bloom_filter>(
                            global_hasher,required_cells,partition,optimal_k);
        }
        tree[node].tokens.push_back(token);
        tree[node].bf->add(keyword);
        mergeLeafNodes(node, start, end);
        size_t parent_node = node / 2;
        while (parent_node >= 1) {
            updateParent(parent_node);
            parent_node /= 2;
        }
        return;
    }

    //非叶子结点
    int mid = (start + end) / 2;
    if (id <= mid)
        update(2 * node, start, mid, id, token, keyword);
    else
        update(2 * node + 1, mid + 1, end, id, token, keyword);
    // tree[node].bf->add(keyword);  // 更新当前节点的 Bloom Filter
    // 仅当 keyword 未被添加时才执行 add
    // if (!tree[node].bf->lookup(keyword)) {
    //     tree[node].bf->add(keyword);
    // }
    // 如果需要，可以在这里合并 tokens 或其他操作
    //std::cout << "Node " << node << " added keyword: " << keyword << ", lookup: " << tree[node].bf->lookup(keyword) << std::endl;
}

void SegmentTree::update(int id, const std::shared_ptr<std::vector<unsigned char>>& token,
                         const std::string& keyword) {
    if(id<0||id>=n) {
        std::cerr << "Error: id out of bounds" << std::endl;
        return;
    }
    update(1, 0, n - 1, id, token, keyword);
}



void SegmentTree::query(size_t node, int start, int end, int l, int r, 
                        std::vector<std::shared_ptr<std::vector<unsigned char>>>& result) {
    if (r < start || l > end) return;
    if (start == end) {
        result.insert(result.end(), tree[node].tokens.begin(), tree[node].tokens.end());
        return;
    }
    int mid = (start + end) / 2;
    query(2 * node, start, mid, l, r, result);
    query(2 * node + 1, mid + 1, end, l, r, result);
}

std::vector<std::shared_ptr<std::vector<unsigned char>>> SegmentTree::query(int l, int r) {
    std::vector<std::shared_ptr<std::vector<unsigned char>>> result;
    if(n==0||tree.empty()) {
        std::cerr << "Error: SegmentTree is empty or size is zero" << std::endl;
        return result;
    }
    query(1, 0, n - 1, l, r, result);
    return result;
}

//输入：区间以及关键词
//输出：包含关键词的区间和对应的tokens
void SegmentTree::getCandidateIntervals(size_t node, int start, int end, 
                                        int l, int r, const std::string& keyword,
                                        std::vector<IntervalResult>& result) {
    if (r < start || l > end ||node>=tree.size()) return;
    if (!tree[node].bf||!tree[node].bf->lookup(keyword)) {
        return;
    }

    // 如果是叶子节点，直接添加 tokens
    if ((start == end && l <= start && start <= r)) {
        if (!tree[node].tokens.empty()) {
            result.emplace_back(start, end, tree[node].tokens);
        }
        return;
    }

    //递归处理内部节点
    if((l <= start && r >= end)&&!tree[node].tokens.empty()) {
        result.emplace_back(start, end, tree[node].tokens);
    }


    int mid = (start + end) / 2;
    getCandidateIntervals(2 * node, start, mid, l, r, keyword, result);
    getCandidateIntervals(2 * node + 1, mid + 1, end, l, r, keyword, result);
}

std::vector<SegmentTree::IntervalResult> 
SegmentTree::getCandidateIntervals(int l, int r, const std::string& keyword) {
    std::vector<IntervalResult> result;
    getCandidateIntervals(1, 0, n - 1, l, r, keyword, result);
    return result;
}