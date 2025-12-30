#include "tdag_bf.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <bf/bloom_filter/basic.hpp>
#include <iostream>


TdagBF::TdagBF(int h, int l, int r, double fp, size_t capacity, size_t seed,bool double_hashing, bool partition)
    : height(h),range({l, r}), middle({-1, -1}), fp(fp), capacity(capacity), seed(seed), double_hashing(double_hashing), partition(partition) {  // 顺序匹配头文件 + 初始化 parent
    // Init BF
    // 在构造函数开头添加 1. 先计算布隆过滤器参数并赋值给成员变量
    required_cells = bf::basic_bloom_filter::m(fp,capacity);
    optimal_k = bf::basic_bloom_filter::k(required_cells, capacity);
    // 假设 partition=true（默认）
    if(partition){
        required_cells += optimal_k - required_cells % optimal_k; // 确保 Bloom Filter 的位向量大小是 k 的倍数
    }
    // 2. 创建全局哈希函数
    global_hasher = bf::make_hasher(optimal_k, seed, double_hashing);
    bf = std::make_shared<bf::basic_bloom_filter>
        (global_hasher, 
         required_cells,
         partition,
         optimal_k);
    

    if (h <= 0) {
        return;  // Leaf
    }

    int midpoint = l + (r - l) / 2;
    int quarter_range = (r - l) / 4;

    if ((r - l) > 1) {
        int mid0 = midpoint - quarter_range;
        int mid1 = midpoint + quarter_range + ((r - l) % 4 > 0 ? 1 : 0);
        middle = {mid0, mid1};
    }

    left  = std::make_shared<TdagBF>(h-1, l, midpoint, fp, capacity, seed, double_hashing, partition);
    // left->parent = shared_from_this();  // 设置 child parent (add this)

    if (midpoint + 1 <= r) {
        right = std::make_shared<TdagBF>(h-1, midpoint+1, r, fp, capacity, seed, double_hashing, partition);
        // right->parent = shared_from_this();  // 设置 child parent (add this)
    }
}


// Factory with BF params
std::shared_ptr<TdagBF> TdagBF::initialize(int height, double fp, size_t capacity, size_t seed) {
    if (height < 0) return nullptr;
    int max_val = (1 << height) - 1;
    auto root = std::make_shared<TdagBF>(height, 0, max_val, fp, capacity, seed);
    root->build_parents();  // 新增：构建完成后设置 parent
    return root;
}

void TdagBF::build_parents() {
    if (left) {
        left->parent = shared_from_this();  // 现在安全
        left->build_parents();
    }
    if (right) {
        right->parent = shared_from_this();  // 现在安全
        right->build_parents();
    }
}

// Insert: Recurse to leaf, add only at leaf, then upward merge
void TdagBF::insert_keyword(int interval_start, int interval_end, const std::vector<std::string>& keywords) {
    insert_keyword_helper(shared_from_this(), interval_start, interval_end, keywords);
}

// ✅ 单字符串重载：直接委托给多字符串版本
void TdagBF::insert_keyword(int interval_start, int interval_end, const std::string& keyword) {
    insert_keyword(interval_start, interval_end, std::vector<std::string>{keyword});
}

// “只在叶子 add，内部结点 OR 子树”——避免重复合并
void TdagBF::insert_keyword_helper(std::shared_ptr<TdagBF> node, int interval_start, int interval_end, const std::vector<std::string>& keywords) {
    if (!node) return;

    // Leaf: Add to BF
    if (node->height <= 0 || node->range.first == node->range.second) {
        for(const auto& keyword : keywords) {
            node->bf->add(keyword);
            //std::cout << "Node " << node << " added keyword: " << keyword << ", lookup: " << tree[node].bf->lookup(keyword) << std::endl;
        }
        
        update_parent(node);  // 向上合并
        return;
    }

    // Internal: Recurse to children (set parent if not set)
    if (node->left && node->interval_contains_interval(node->left->range, {interval_start, interval_end})) {
        auto parent_sp = node->shared_from_this();  // 使用 shared_from_this()
        if (node->left->parent != parent_sp) {
            node->left->parent = parent_sp;
        }
        insert_keyword_helper(node->left, interval_start, interval_end, keywords);
    }
    if (node->right && node->interval_contains_interval(node->right->range, {interval_start, interval_end})) {
        auto parent_sp = node->shared_from_this();
        if (node->right->parent != parent_sp) {
            node->right->parent = parent_sp;
        }
        insert_keyword_helper(node->right, interval_start, interval_end, keywords);
    }
    // // Middle: Add to current
    // if (node->middle.first != -1 && node->interval_contains_interval(node->middle, {interval_start, interval_end})) {
    //     node->bf->add(keyword);
    // }

    // // Merge children to current
    // if (node->left && node->left->bf) {
    //     node->bf = merge_bloom(node->bf, node->left->bf);
    // }
    // if (node->right && node->right->bf) {
    //     node->bf = merge_bloom(node->bf, node->right->bf);
    // }

    // 非叶：只递归到包含该区间的子树；不在这里 add，不在这里 OR
    update_parent(node);// 让父链做一次自底向上 OR 即可
}

// void TdagBF::update_parent(std::shared_ptr<TdagBF> node) {
//     if (!node) return;
//     auto parent_sp = node->parent.lock();  // Safe lock
//     if (!parent_sp) return;  // Root or no parent

//     // Merge to parent
//     parent_sp->bf = merge_bloom(parent_sp->bf, node->bf);

//     // Recurse up
//     update_parent(parent_sp);
// }

void TdagBF::update_parent(std::shared_ptr<TdagBF> node) {
    if (!node) return;
    auto p = node->parent;
    if (!p) return;

    // 如果父有左右孩子，先看左右是否“足够相似”
    if (p->left && p->right && p->left->bf && p->right->bf) {
        const auto& L = p->left->bf->storage();
        const auto& R = p->right->bf->storage();
        if (L.size() == R.size() && !L.empty()) {
            size_t hd = 0;
            for (size_t i = 0; i < L.size(); ++i) {
                unsigned long long x = static_cast<unsigned long long>(L[i] ^ R[i]);
                hd += __builtin_popcountll(x);
            }
            // 注意：真实位数应是 word_count * sizeof(word) * 8
            const size_t total_bits = L.size() * sizeof(L[0]) * 8;
            double rel = total_bits ? (static_cast<double>(hd) / total_bits) : 1.0;

            auto merged_lr = merge_bloom(p->left->bf, p->right->bf);
            if (rel < THRESHOLD) {
                // 共享：三者同指针
                p->left->bf  = merged_lr;
                p->right->bf = merged_lr;
                p->bf        = merged_lr;
            } else {
                // 不共享：父独享
                p->bf = merged_lr;
            }
        } else {
            // 尺寸不等或空：保守 OR 当前 node→parent
            p->bf = merge_bloom(p->bf, node->bf);
        }
    } else {
        // 只有一个孩子：把该子 OR 上去（父独享）
        p->bf = merge_bloom(p->bf, node->bf);
    }

    update_parent(p);
}


// // Hamming distance and merge with sharing logic
// size_t TdagBF::hamming_distance(const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
//                                 const std::shared_ptr<bf::basic_bloom_filter>& bf2) const {
//     if (!bf1 || !bf2) return std::numeric_limits<size_t>::max();
//     auto bits1 = bf1->storage();
//     auto bits2 = bf2->storage();
//     size_t distance = 0;
//     for (size_t i = 0; i < bits1.size(); ++i) {
//         distance += __builtin_popcount(bits1[i] ^ bits2[i]); // 使用位异或计算 Hamming 距离
//     }
//     return distance;
// }

// Hamming distance logic
// TODO：
// 输入：两个 Bloom Filter 指针
// 输出：它们的 Hamming 距离（位向量不同位数）
// 注意：如果任一指针为空，或位向量尺寸不同，返回最大
size_t TdagBF::hamming_distance(const std::shared_ptr<bf::basic_bloom_filter>& bf1, 
                                const std::shared_ptr<bf::basic_bloom_filter>& bf2) const {
    if (!bf1 || !bf2) return std::numeric_limits<size_t>::max();

    const auto& bits1 = bf1->storage();
    const auto& bits2 = bf2->storage();
    if (bits1.size() != bits2.size()) {
        // 尺寸不同无法比较，返回大距离
        return std::numeric_limits<size_t>::max();
    }

    size_t distance = 0;
    for (size_t i = 0; i < bits1.size(); ++i) {
        // storage() 通常以 64-bit word 存放；使用 popcountll 更安全
        unsigned long long x = static_cast<unsigned long long>(bits1[i] ^ bits2[i]);
        distance += __builtin_popcountll(x);
    }
    return distance;
}



//TODO：
//输入：bfA和bfB
//输出：
// 如果两个bf相似，则bf1和bf2进行or运行为父节点，bf1和bf2以及他们的父节点，共享一个指针
// 如果两个bf不相似，则bf1 or bf2返回一个新的指针，为父节点
std::shared_ptr<bf::basic_bloom_filter> TdagBF::merge_bloom(
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

    size_t dist = TdagBF::hamming_distance(bf1,bf2);

    const size_t total_bits = bits1.size() * sizeof(bits1[0]) * 8;
    const double rel = total_bits > 0 ? (static_cast<double>(dist) / static_cast<double>(total_bits)) : 1.0;


    bf::bitvector merged_bit = bits1 | bits2;
    // 使用 bf1 的哈希函数创建新的布隆过滤器
    // auto merged_bf = std::make_shared<bf::basic_bloom_filter>(global_hasher, required_cells, partition,optimal_k);
    // merged_bf->storage() = merged_bits; // 设置合并后的位向量
    // bf1->set_storage(std::move(merged_bit)); 
    // 基于当前节点全局参数创建“全新” BF（不要就地 set_storage 到 bf1）
    if (rel < THRESHOLD) {
        // —— 相似：复用 bf1 指针（就地写回 OR 结果）
        // 1. 高度相似：内存共享
        // 计算 OR 结果，并直接修改 bf1 的位向量。
        // 返回 bf1 的指针。调用者 (update_parent) 会将这个共享指针赋给父节点和两个子节点。
        // 这样，三个节点将指向同一个 BF 实例，实现最大程度的内存复用。
        bf1->set_storage(std::move(merged_bit));
        return bf1; // 满足 EXPECT_EQ(merged_sim, bf1)
    } else {
        // —— 不相似：返回“新对象”，与 bf1 指针不同
        // 2. 差异较大：创建新的父节点 BF
        // 创建一个新的 BF 实例来存储合并结果，保持子节点的独立性。
        // 这会消耗更多内存，但保证了子树的BF不会因为合并而“污染”彼此。
        auto out = std::make_shared<bf::basic_bloom_filter>(
            global_hasher, required_cells, partition, optimal_k);
        out->set_storage(std::move(merged_bit));
        return out; // 满足 EXPECT_NE(merged_diff, bf1)
    }
}

// BF matches
bool TdagBF::bf_matches(const std::vector<std::string>& keywords) const {
    if (!bf) return false;
    for (const auto& kw : keywords) {
        if (bf->lookup(kw)) return true;
    }
    return false;
}

// TODO：
// 输入：query_range, keywords
// 输出：单一覆盖区间
// 要求：覆盖区间必须在树中存在（即某节点的 range 或 middle）
//      覆盖区间必须包含 query_range
//      覆盖区间的 BF 必须与 keywords 有匹配（允许误判）
//      优先选择更小的覆盖区间  （更精细的时间粒度）
// Get single cover with BF prune

// 对 get_single_range_cover(query_range, keywords)，判定“有效覆盖”的充要条件统一成 4 条：
// 存在性：覆盖区间必须是树上“已定义”的节点（range）或“注入节点”（middle）。
// 包含性：覆盖区间完全包含 query_range。
// BF 匹配：该覆盖节点的 BF 对 keywords 中至少一个返回可能命中（允许 FP）。
// 极小性：在满足 1–3 的所有覆盖里，选择区间长度最小的（时间粒度最细）。若有多解等长，则选更靠下层（深度更大）的；再若仍冲突，优先选BF 更稀疏（置位更少，FP 风险更低）的。
// std::pair<int, int> TdagBF::get_single_range_cover(const std::pair<int, int>& query_range, const std::vector<std::string>& keywords) const {
//     auto res = get_single_range_cover_helper(shared_from_this(), query_range, keywords);
//     if (res.first == -1) {
//         throw std::runtime_error("No BF-matching cover found for range [" + std::to_string(query_range.first) + "," + std::to_string(query_range.second) + "]");
//     }
//     return res;
// }

// 计算区间的长度
static inline int range_size(const std::pair<int, int>& range) {
    if (range.first == -1) return std::numeric_limits<int>::max();
    return range.second - range.first;
}

static inline double bf_load(const std::shared_ptr<bf::basic_bloom_filter>& b) {
    if (!b) return 1.0;
    const auto& v = b->storage();
    const size_t n = v.size();
    if (n == 0) return 0.0;
    size_t ones = 0;
    // 避免 range-for；部分 bitvector 无迭代器
    for (size_t i = 0; i < n; ++i) {
        // v[i] 一般是 64-bit word；用 popcountll 最稳妥
        unsigned long long word = static_cast<unsigned long long>(v[i]);
        ones += __builtin_popcountll(word);
    }
    const size_t total_bits = n * sizeof(v[0]) * 8;
    return total_bits ? static_cast<double>(ones) / static_cast<double>(total_bits) : 1.0;
}

// 若 range 恰好是某层标准 [L,R]，它会出现在沿根向下“唯一包含链”上；
// 若是某父的 middle，也可在下行过程中命中 “node->middle == range”。
std::shared_ptr<const TdagBF> locate_node_for_range(
    std::shared_ptr<const TdagBF> root,
    const std::pair<int,int>& target)
{
    auto cur = root;
    while (cur) {
        if (cur->range == target) return cur;      // 标准节点命中
        if (cur->middle == target) return cur;     // 注入节点命中

        // 继续向唯一可能的子树下行（必须保证 target ⊆ child->range）
        bool to_left  = cur->left  && cur->interval_contains_interval(cur->left->range,  target);
        bool to_right = cur->right && cur->interval_contains_interval(cur->right->range, target);
        if      (to_left)  cur = cur->left;
        else if (to_right) cur = cur->right;
        else               break; // 无法继续匹配，节点不存在
    }
    return nullptr;
}



// std::pair<int, int> TdagBF::get_single_range_cover_helper(const std::shared_ptr<const TdagBF>& node, 
//                                                           const std::pair<int, int>& query_range, 
    
//                                                           const std::vector<std::string>& keywords) const {

//     // --- 1. 前置剪枝 (Pre-order Pruning) ---
//     // 如果当前节点无法覆盖查询，或其 BF 不匹配，则整个子树无效。
//     if (!node || !interval_contains_interval(node->range, query_range)) {
//         return {-1, -1};
//     }

//     // BF prune: If no match, skip this subtree
//     if (!node->bf_matches(keywords)) {
//         return {-1, -1};
//     }


//     // --- 2. 递归深入 (Post-order Traversal Logic) ---
//     // 探索子节点，寻找更小的可能覆盖区间。
//     // Middle better fit?
//     if (node->middle.first != -1 && interval_contains_interval(node->middle, query_range)) {
//         bool left_ok = node->left ? interval_contains_interval(node->left->range, query_range) : false;
//         bool right_ok = node->right ? interval_contains_interval(node->right->range, query_range) : false;
//         if (!left_ok && !right_ok) {
//             // Check middle BF (use current if shared)
//             if (node->bf_matches(keywords)) {  // Already checked node, assume middle shares
//                 return node->middle;
//             }
//         }
//     }

//     // Recurse children
//     if (node->left && interval_contains_interval(node->left->range, query_range)) {
//         auto child_cover = get_single_range_cover_helper(node->left, query_range, keywords);
//         if (child_cover.first != -1) return child_cover;
//     }
//     if (node->right && interval_contains_interval(node->right->range, query_range)) {
//         auto child_cover = get_single_range_cover_helper(node->right, query_range, keywords);
//         if (child_cover.first != -1) return child_cover;
//     }

//     // Current is best (already BF-checked)
//     return node->range;
// }

// 交集内命中：只有与 q 有交集的子分支才参与 BF 剪枝
bool TdagBF::has_match_within(const std::shared_ptr<const TdagBF>& node,
                              std::pair<int,int> q,
                              const std::vector<std::string>& kws) const {
    if (!node) return false;
    // 无交集直接剪掉
    if (q.second < node->range.first || q.first > node->range.second) return false;

    // 叶子：叶 BF 命中即返回
    if (!node->left && !node->right) {
        return node->bf_matches(kws);
    }

    // 递归只进入“与 q 有交集”的子分支（middle 视作与其两侧覆盖的子区间一起被父层聚合）
    bool hit = false;
    if (node->left  && !(q.second < node->left->range.first  || q.first > node->left->range.second))
        hit = hit || has_match_within(node->left,  q, kws);
    if (node->right && !(q.second < node->right->range.first || q.first > node->right->range.second))
        hit = hit || has_match_within(node->right, q, kws);

    // 可选：若你为 middle 单独建了 BF，也可在此判断与 q 的交集
    // if (node->middle != invalid && intersect(node->middle, q)) hit |= middle_bf_matches(...);

    return hit;
}




// 层序 + 两候选 + 存在性 + BF 剪枝 + 稀疏度择优
// 每层node的左右间隔大小都为2的幂次
std::pair<int,int>
TdagBF::get_single_range_cover(const std::pair<int,int>& q,
                               const std::vector<std::string>& kws) const
{
    if (q.first > q.second) throw std::invalid_argument("bad range");

    // 若根 BF 都不匹配，直接剪掉整棵树
    if (!bf_matches(kws)) {
        throw std::runtime_error("No BF-matching cover for [" +
            std::to_string(q.first) + "," + std::to_string(q.second) + "]");
    }

    // 从最小 2^k 覆盖块开始（next_pow2）
    int len = q.second - q.first + 1;
    int s = 1; while (s < len) s <<= 1;
    const int domain_lo = range.first;//当前（通常是根）TDAG 节点的时间域上下界
    const int domain_hi = range.second;
    
    // 用 const 版本的 self 进入定位函数
    auto self = std::const_pointer_cast<const TdagBF>(shared_from_this());

    for (; s <= (domain_hi - domain_lo + 1); s <<= 1) {//在该层以半步 (s/2) 对齐的方式平移节点，因此“能覆盖 q 的候选起点”只有两个：
        int offset = std::max(1, s >> 1);

        auto clamp = [&](int x) {
            // 让候选块完全落在域中
            return std::min(std::max(x, domain_lo), domain_hi - (s - 1));
        };

        int start_left  = clamp((q.first / offset) * offset);
        int start_right = clamp(((q.second + 1 + offset - 1) / offset) * offset - s);

        std::pair<int,int> candA{start_left,  start_left  + s - 1};
        std::pair<int,int> candB{start_right, start_right + s - 1};

        // 去重
        if (candA == candB) {
            auto node = locate_node_for_range(self, candA);
            // if (node && node->bf_matches(kws)) return candA; //返回or的情况
            if (node && has_match_within(node, q, kws)) return candA; //返回and的情况
        } else {
            // 先做存在性 + BF 剪枝
            std::pair<int,int> best{-1,-1};
            double best_load = 1.0;

            auto try_one = [&](const std::pair<int,int>& c) {
                auto node = locate_node_for_range(self, c);
                if (!node) return; 
                // if (!node->bf_matches(kws)) return; // 返回or的情况
                
                //返回and的情况
                if (!has_match_within(node, q, kws)) return;
                
                double load = bf_load(node->bf);
                // 极小性：本层等长，优先更稀疏；可加“更深优先”的 tie-break（此处同层深度相同）
                if (best.first == -1 || load < best_load) {
                    best = c; best_load = load;
                }
            };

            try_one(candA);
            try_one(candB);

            if (best.first != -1) return best;
        }
        // 放宽块大小，继续尝试更大覆盖
    }
    return {-1, -1};
}



// Descend tree (unchanged)
std::vector<std::pair<int, int>> TdagBF::descend_tree(int val, std::pair<int, int> rnge) const {
    std::vector<std::pair<int, int>> rnges;
    while (rnge.first != val || rnge.second != val) {
        if (std::find(rnges.begin(), rnges.end(), rnge) == rnges.end()) {
            rnges.push_back(rnge);
        }
        int middle_val = rnge.first + (rnge.second - rnge.first) / 2;
        int quarter = (rnge.second - rnge.first) / 4;
        int mid0 = middle_val - quarter;
        int mid1 = middle_val + quarter + ((rnge.second - rnge.first) % 4 > 0 ? 1 : 0);
        if (val >= mid0 && val <= mid1 && (rnge.second - rnge.first) > 1) {
            if (std::find(rnges.begin(), rnges.end(), std::make_pair(mid0, mid1)) == rnges.end()) {
                rnges.push_back({mid0, mid1});
            }
        }
        if (val <= middle_val) {
            rnge.second = middle_val;
        } else {
            rnge.first = middle_val + 1;
        }
    }
    rnges.push_back({val, val});
    std::sort(rnges.begin(), rnges.end());
    auto it = std::unique(rnges.begin(), rnges.end());
    rnges.resize(it - rnges.begin());
    return rnges;
}

bool TdagBF::interval_contains_interval(const std::pair<int, int>& main, const std::pair<int, int>& secondary) const {
    return main.first <= secondary.first && main.second >= secondary.second;
}


void TdagBF::update_src_cover(const std::pair<int,int>& q, const std::string& kw){
    auto cover = get_single_range_cover(q, /*keywords=*/{kw}); // 先用 BF 允许覆盖
    // 沿树下行找到 cover 对应的真实节点
    auto cur = shared_from_this();
    while (cur && cur->range != cover && (cur->left || cur->right)) {
        if (cur->left && interval_contains_interval(cur->left->range, cover))   cur = cur->left;
        else if (cur->right && interval_contains_interval(cur->right->range,cover)) cur = cur->right;
        else break;
    }
    cur->bf->add(kw);
    update_parent(cur); // 向上 OR
}


std::vector<std::pair<int,int>> TdagBF::covering_intervals_for_leaf(int leaf) const {
    std::vector<std::pair<int,int>> out;
    // 边界：leaf 不在当前 TDAG 定义域内 ⇒ 返回空
    if (leaf < range.first || leaf > range.second) return out;

    // 从当前（通常是根）开始一路向下
    auto node = this; // 原生指针即可；不需要 shared_ptr
    while (node) {
        // 1) 记录当前结点的标准区间 range
        if (out.empty() || out.back() != node->range) {
            out.push_back(node->range);
        }
        // 2) 如果 middle 存在且也覆盖 leaf，则记录 middle 区间
        if (node->middle.first != -1) {
            const auto& m = node->middle;
            if (leaf >= m.first && leaf <= m.second) {
                if (out.back() != m) out.push_back(m);
            }
        }
        // 3) 到叶则停止
        if (!node->left && !node->right) break;

        // 4) 沿唯一下行路径进入下一层（TDAG 的二分骨架）
        const int mid = node->range.first + (node->range.second - node->range.first) / 2;
        node = (leaf <= mid) ? node->left.get() : node->right.get();
    }

    // 5) 最后把叶子自身区间 [leaf, leaf] 放进去，确保“点区间”也被返回
    if (out.empty() || out.back() != std::make_pair(leaf, leaf)) {
        out.emplace_back(leaf, leaf);
    }

    // 可选：如果担心重复（极少见），可以去重；默认保持自顶向下的顺序更便于调试
    // out.erase(std::unique(out.begin(), out.end()), out.end());

    return out;
}
