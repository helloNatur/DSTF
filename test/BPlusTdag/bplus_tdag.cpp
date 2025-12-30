#include "bplus_tdag.h"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <queue>
#include <ctime>

#ifdef DEBUG
#define DEBUG_LOG(x) std::cout << x << std::endl
#else
#define DEBUG_LOG(x)
#endif

BPlusTdag::BPlusTdag(int d,
                     const std::vector<double>& min_vals,
                     const std::vector<double>& max_vals,
                     int levels) 
        : root_(std::make_shared<BPlusTdagNode>(true)),
          ccs(std::make_shared<CubeCode>(d, min_vals, max_vals, levels)) {
    root_->parent.reset();  // Root has no parent
}

// Find leaf node for a given key (fixed boundary)
// std::shared_ptr<BPlusTdagNode> BPlusTdag::findLeafNode(long long key) const {
//     auto node = root_;
//     while (!node->isLeaf && !node->children.empty()) {
//         int index = node->findKeyIndex(key);  // lower_bound
//         index = std::min(index, static_cast<int>(node->children.size() - 1));  // 修复：边界检查
//         DEBUG_LOG("findLeafNode index=" << index << " for key=" << key);
//         node = node->children[index];
//     }
//     return node;
// }

// Find leaf node for a given key
std::shared_ptr<BPlusTdagNode> BPlusTdag::findLeafNode(long long key) const {
    if (!root_) return nullptr;
    auto node = root_;
    while (!node->isLeaf) {
        // int index = node->findKeyIndex(key);
        auto it = std::upper_bound(node->keys.begin(),node->keys.end(),key);
        int index = it-node->keys.begin();

        //边界检查
        if (index >= static_cast<int>(node->children.size())) {
            DEBUG_LOG("WARNING: Adjusting index in findLeafNode from " 
                      << index << " to " << (node->children.size() - 1));
            index = node->children.size() - 1;
        }

        node = node->children[index];
    }
    return node;
}

// // Insert a new day's TdagBF
// void BPlusTdag::insert(long long day_ts, std::shared_ptr<TdagBF> tdag) {
//     if (!root_) {
//         root_ = std::make_shared<BPlusTdagNode>(true);
//         root_->keys.push_back(day_ts);
//         root_->day_tdag.push_back(tdag);
//         root_->parent.reset();
//         DEBUG_LOG("Inserted key " << day_ts << " into new root");
//         return;
//     }
//     insertInternal(day_ts, tdag, root_);
// }

// Internal insertion logic (fixed child index boundary)
// void BPlusTdag::insertInternal(long long day_ts, std::shared_ptr<TdagBF> tdag, std::shared_ptr<BPlusTdagNode> node) {
//     if (!node) {
//         DEBUG_LOG("Node is null, cannot insert key: " << day_ts);
//         return;
//     }

//     if (node->isLeaf) {
//         auto it = std::lower_bound(node->keys.begin(), node->keys.end(), day_ts);
//         int index = static_cast<int>(it - node->keys.begin());

//         if (it != node->keys.end() && *it == day_ts) {
//             node->day_tdag[index] = tdag;
//             DEBUG_LOG("Updated key " << day_ts << " in leaf node");
//             return;
//         }

//         node->keys.insert(it, day_ts);
//         node->day_tdag.insert(node->day_tdag.begin() + index, tdag);
//         DEBUG_LOG("Inserted key " << day_ts << " into leaf node");

//         if (node->keys.size() >= order_) {
//             splitNode(node, node->parent);
//         }
//     // } else {
//     //     int index = node->findKeyIndex(day_ts);
//     //     index = std::min(index, static_cast<int>(node->children.size() - 1));  // 修复：边界检查
//     } else {
//         // 与分隔键语义一致：等于分隔键走右子树
//         auto it = std::upper_bound(node->keys.begin(), node->keys.end(), day_ts);
//         int index = static_cast<int>(it - node->keys.begin());
//         if (index >= static_cast<int>(node->children.size())) {
//             index = static_cast<int>(node->children.size() - 1);
//         }
//         DEBUG_LOG("insertInternal child index=" << index << " for key=" << day_ts);
//         auto child = node->children[index];
//         insertInternal(day_ts, tdag, child);
//         if (child && child->keys.size() >= order_) {
//             splitNode(child, node);
//         }
//     }
// }

void BPlusTdag::insertInternal(long long day_ts, std::shared_ptr<TdagBF> tdag, std::shared_ptr<BPlusTdagNode> node) {
    while(true) { // 改为循环结构
        if (node->isLeaf) {
            // ... (叶子插入逻辑不变)
            auto it = std::lower_bound(node->keys.begin(), node->keys.end(), day_ts);
            int index = static_cast<int>(it - node->keys.begin());
            if (it != node->keys.end() && *it == day_ts) {
                node->day_tdag[index] = tdag;
                return;
            }
            node->keys.insert(it, day_ts);
            node->day_tdag.insert(node->day_tdag.begin() + index, tdag);
            
            if (node->keys.size() >= order_) {
                splitNode(node, node->parent); // 使用 .lock() 获取 shared_ptr
            }
            return; // 叶子层插入完成
        } else {
            auto it = std::upper_bound(node->keys.begin(), node->keys.end(), day_ts);
            int index = static_cast<int>(it - node->keys.begin());
            
            auto child = node->children[index];
            
            // 检查子节点是否需要分裂
            if (child->keys.size() >= order_) {
                splitNode(child, node);
                // 分裂后，需要重新确定应该插入到哪个子节点
                it = std::upper_bound(node->keys.begin(), node->keys.end(), day_ts);
                index = static_cast<int>(it - node->keys.begin());
                child = node->children[index];
            }
            node = child; // 向下移动一层，继续循环
        }
    }
}

// bplus_tdag.cpp

// 创建一个新的私有辅助函数
std::shared_ptr<TdagBF> BPlusTdag::findOrCreateTdag(long long day_ts) {
    if (!root_) {
        root_ = std::make_shared<BPlusTdagNode>(true);
    }
    auto leaf = findLeafNode(day_ts);
    
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), day_ts);
    int index = it - leaf->keys.begin();

    // 如果找到了 key
    if (it != leaf->keys.end() && *it == day_ts) {
        return leaf->day_tdag[index];
    }
    
    // 如果没找到，则在此处创建并插入
    auto new_tdag = TdagBF::initialize(8, 0.01, 100, 0); // 使用默认参数创建
    leaf->keys.insert(it, day_ts);
    leaf->day_tdag.insert(leaf->day_tdag.begin() + index, new_tdag);

    if (leaf->keys.size() >= order_) {
        splitNode(leaf, leaf->parent);
    }

    return new_tdag;
}

// 重写 update_point 函数
bool BPlusTdag::update_point(long long day_ts, int interval, const std::vector<std::string>& keywords) {
    // 调用新的辅助函数，一步到位
    auto tdag = findOrCreateTdag(day_ts);
    if (tdag) {
        tdag->insert_keyword(interval, interval, keywords);
        return true;
    }
    return false; // 理论上不会到达这里
}

// 同时，为了保持公共接口的完整性，可以保留旧的 insert，但让其内部逻辑更清晰
void BPlusTdag::insert(long long day_ts, std::shared_ptr<TdagBF> tdag) {
    if (!root_) {
        root_ = std::make_shared<BPlusTdagNode>(true);
    }
    auto leaf = findLeafNode(day_ts);

    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), day_ts);
    int index = it - leaf->keys.begin();

    if (it != leaf->keys.end() && *it == day_ts) {
        leaf->day_tdag[index] = tdag; // 覆盖
    } else {
        leaf->keys.insert(it, day_ts);
        leaf->day_tdag.insert(leaf->day_tdag.begin() + index, tdag); // 插入
    }

    if (leaf->keys.size() >= order_) {
        splitNode(leaf, leaf->parent);
    }
}

// Split node (optimized resize/assign order)
// void BPlusTdag::splitNode(std::shared_ptr<BPlusTdagNode> node, std::shared_ptr<BPlusTdagNode> parent) {
//     if (!node) return;
//     auto new_node = std::make_shared<BPlusTdagNode>(node->isLeaf);
//     int mid = static_cast<int>(node->keys.size() / 2);
//     long long mid_key;

//     if (node->isLeaf) {
//         // Leaf: copy right half first
//         new_node->keys = std::vector<long long>(node->keys.begin() + mid, node->keys.end());
//         new_node->day_tdag = std::vector<std::shared_ptr<TdagBF>>(node->day_tdag.begin() + mid, node->day_tdag.end());
//         node->keys.resize(mid);
//         node->day_tdag.resize(mid);
//         new_node->next = node->next;
//         if (node->next) {
//             node->next->parent = new_node;
//         }
//         node->next = new_node;
//         new_node->parent = parent;
//         if (!new_node->keys.empty()) {
//             // Promote a COPY of the first key of the right leaf to parent.
//             // Do NOT erase it from the leaf (B+ tree keeps all keys in leaves).
//             mid_key = new_node->keys.front();
//             // new_node->keys.erase(new_node->keys.begin());
//         } else {
//             mid_key = node->keys.back();  // Edge case
//         }
//         DEBUG_LOG("Leaf split: mid_key=" << mid_key << ", left=" << node->keys.size() << ", right=" << new_node->keys.size());
//     } else {
//         // Internal: copy right parts first
//         if (mid + 1 < static_cast<int>(node->keys.size())) {
//             new_node->keys = std::vector<long long>(node->keys.begin() + mid + 1, node->keys.end());
//             new_node->children = std::vector<std::shared_ptr<BPlusTdagNode>>(node->children.begin() + mid + 1, node->children.end());
//         }
//         mid_key = node->keys[mid];
//         node->keys.resize(mid);
//         node->children.resize(mid + 1);
//         new_node->parent = parent;
//         // Update children parents
//         for (auto& ch : node->children) {
//             if (ch) ch->parent = node;
//         }
//         for (auto& ch : new_node->children) {
//             if (ch) ch->parent = new_node;
//         }
//         DEBUG_LOG("Internal split: mid_key=" << mid_key << ", left keys=" << node->keys.size() 
//                   << ", children=" << node->children.size() << ", right keys=" << new_node->keys.size() 
//                   << ", children=" << new_node->children.size());
//     }

//     if (!parent) {
//         parent = std::make_shared<BPlusTdagNode>(false);
//         parent->keys = {mid_key};
//         parent->children = {node, new_node};
//         node->parent = parent;
//         new_node->parent = parent;
//         root_ = parent;
//         DEBUG_LOG("Root split");
//         return;
//     }

//     auto it = std::lower_bound(parent->keys.begin(), parent->keys.end(), mid_key);
//     int idx = static_cast<int>(it - parent->keys.begin());
//     parent->keys.insert(it, mid_key);
//     parent->children.insert(parent->children.begin() + idx + 1, new_node);
//     new_node->parent = parent;

//     if (parent->keys.size() >= order_) {
//         splitNode(parent, parent->parent);
//     }
// }

void BPlusTdag::splitNode(std::shared_ptr<BPlusTdagNode> node, std::shared_ptr<BPlusTdagNode> parent) {
    // auto new_node = std::make_shared<BPlusTdagNode>(node->isLeaf);
    // int mid = node->keys.size()/ 2; //均匀分裂

    // //把后一半的keys以及segment trees移动到新节点
    // new_node->keys.assign(node->keys.begin() + mid, node->keys.end());
    // if (node->isLeaf) {  //叶子节点，把后一半的segmenttree移动到新节点
    //     new_node->day_tdag.assign(node->day_tdag.begin() + mid, node->day_tdag.end());
    //     node->keys.resize(mid);
    //     node->day_tdag.resize(mid);
    //     new_node->next = node->next;
    //     new_node->parent = node->parent; // Maintain parent link
    //     if (node->next) {
    //         node->next->parent = new_node;
    //     }
    //     node->next = new_node;
    // } else {//处理内部节点
    //     new_node->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    //     new_node->children.assign(node->children.begin() + mid + 1, node->children.end());

    //     //调整原节点大小
    //     node->children.resize(mid+1);
    //     node->keys.resize(mid);

    //     //更新子节点的父指针
    //     for(auto& child : new_node->children) {
    //         child->parent = new_node; // Maintain parent link
    //     }
        
    // }

    // //上升的键：内部节点用中间键，叶子节点用新节点第一个键
    // long long up_key = node->isLeaf ? new_node->keys[0] : node->keys[mid];
    // if(!node->isLeaf) {
    //     node->keys.resize(mid);
    // } 

    //Debug output
    // DEBUG_LOG( "Splitting node.Original keys: "<<[&node]() {
    //     std::stringstream ss;
    //     for (const auto& k : node->keys) {
    //         ss << k << " ";
    //     }
    //     return ss.str();
    // }() );
    
    // DEBUG_LOG( "\nNew node keys: "<<[&new_node]() {
    //     std::stringstream ss;
    //     for (const auto& k : new_node->keys) {
    //         ss << k << " ";
    //     }
    //     return ss.str();
    // }() );
   
    // DEBUG_LOG( "\nUp key: " << up_key );

    auto new_node = std::make_shared<BPlusTdagNode>(node->isLeaf);
    // **核心修正1**: 立即为新节点设置正确的父指针。
    // 无论后续发生什么，它的父节点就是当前传入的parent。
    new_node->parent = parent;
    int mid = static_cast<int>(node->keys.size() / 2); // 均匀分裂
    long long up_key = 0;

    if (node->isLeaf) {
        // 叶：右半拷贝到 new_node，父分隔键=右半第一个，但不从叶子删除
        new_node->keys.assign(node->keys.begin() + mid, node->keys.end());
        new_node->day_tdag.assign(node->day_tdag.begin() + mid, node->day_tdag.end());
        node->keys.resize(mid);
        node->day_tdag.resize(mid);
        new_node->next = node->next;
        // if (node->next) node->next->parent = new_node;
        //  修正：兄弟叶子的 parent 永远指向内部结点，不能设为叶子
        if (node->next) node->next->parent = parent;
        node->next = new_node;
        up_key = new_node->keys.front();
    } else {
        // 内：先保存提升键，再做 resize/split
        long long promote = node->keys[mid];
        // 右半（不含提升键）拷到 new_node
        new_node->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
        new_node->children.assign(node->children.begin() + mid + 1, node->children.end());
        // 左半保留（含 0..mid-1），孩子数 mid+1
        node->keys.resize(mid);
        node->children.resize(mid + 1);
        // 修正新结点孩子父指针
        for (auto &ch : new_node->children) if (ch) ch->parent = new_node;
        up_key = promote;
    }

    //update parent
    if (!parent) {
        // **核心修正2**: 正确处理根分裂
        // 如果没有父节点，说明当前分裂的是根。
        auto new_root = std::make_shared<BPlusTdagNode>(false);
        new_root->keys.push_back(up_key);
        new_root->children.push_back(node);
        new_root->children.push_back(new_node);
        root_ = new_root;
        // 新分裂出的两个子节点，其父指针必须指向新的根
        node->parent = root_;
        new_node->parent = root_; // new_node 之前 parent 是 nullptr，现在指向 new_root
    } else {
        // auto it = std::lower_bound(parent->keys.begin(), parent->keys.end(), up_key);
        auto it = std::lower_bound(parent->keys.begin(), parent->keys.end(), up_key);
        int index = it - parent->keys.begin();

        parent->keys.insert(it, up_key);
        // parent->children[index] = node;
        parent->children.insert(parent->children.begin() + index + 1, new_node);

        // node->parent = parent;
        // new_node->parent = parent;

        // //检查父节点是否需要分裂
        // if(parent->keys.size()>=order_){
        //     splitNode(parent,parent->parent);
        // }
    }

    // 一致性检查（调试用）
    if (!node->isLeaf) {
        if (node->keys.size() + 1 != node->children.size()) {
            DEBUG_LOG("Consistency error: keys(" << node->keys.size() 
                      << ") + 1 != children(" << node->children.size() << ")");
        }
        if (new_node->keys.size() + 1 != new_node->children.size()) {
            DEBUG_LOG("Consistency error (new node): keys(" << new_node->keys.size() 
                      << ") + 1 != children(" << new_node->children.size() << ")");
        }
    }
}

// Search
std::shared_ptr<TdagBF> BPlusTdag::search(long long day_ts) const {
    auto leaf = findLeafNode(day_ts);
    if (!leaf) return nullptr;
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), day_ts);
    if (it != leaf->keys.end() && *it == day_ts) {
        return leaf->day_tdag[static_cast<size_t>(it - leaf->keys.begin())];
    }
    return nullptr;
}

// Range search (fixed traversal)
std::vector<std::pair<long long, std::shared_ptr<TdagBF>>> 
BPlusTdag::rangeSearch(long long start_day_ts, long long end_day_ts) const {
    std::vector<std::pair<long long, std::shared_ptr<TdagBF>>> result;
    if (!root_) return result;
    auto leaf = findLeafNode(start_day_ts);
    // if (!leaf || leaf->keys.empty()) return result;
    // while (leaf && leaf->keys.front() <= end_day_ts) {  // front() is min key
    while (leaf) {
        if (leaf->keys.empty()) {        // 空叶保护（防结构异常或边界情况）
            leaf = leaf->next;
            continue;
        }
        if (leaf->keys.front() > end_day_ts) break; // 超过上界就停
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] >= start_day_ts && leaf->keys[i] <= end_day_ts) {
                result.emplace_back(leaf->keys[i], leaf->day_tdag[i]);
            }
        }
        leaf = leaf->next;
    }
    DEBUG_LOG("Range search returned " << result.size() << " items");
    return result;
}

// Update point
// bool BPlusTdag::update_point(long long day_ts, int interval, const std::vector<std::string>& keywords) {
//     auto tdag = search(day_ts);
//     if (!tdag) {
//         tdag = TdagBF::initialize(8, 0.01, 100, 0);
//         insert(day_ts, tdag);
//     }
//     if (tdag) {
//         tdag->insert_keyword(interval, interval, keywords);
//         return true;
//     }
//     return false;
// }

// bool BPlusTdag::update_point(long long day_ts, int interval, const std::vector<std::string>& keywords) {

//     auto leaf = findLeafNode(day_ts);
//     int index = leaf->findKeyIndex(day_ts);
//     if (index >= static_cast<int>(leaf->keys.size()) || leaf->keys[index] != day_ts) {
//         std::cerr << "Key " << day_ts << " not found for update" << std::endl;
//         return false;
//     }
//     if (leaf->day_tdag[index]) {
//         for (int i = interval; i <= interval; ++i) {
//             leaf->day_tdag[index]->insert_keyword(i, i, keywords);
//         }
//         return true;
//     }
//     return false;
// }

// Stubs
bool BPlusTdag::remove(long long key) {
    // auto leaf = findLeafNode(key);
    if (!root_) return false;
    auto leaf = findLeafNode(key);
    if (!leaf) return false;
    auto it = std::find(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end()) {
        std::cerr << "Key " << key << " not found" << std::endl;
        return false;
    }
    int index = it - leaf->keys.begin();
    leaf->keys.erase(it);
    leaf->day_tdag.erase(leaf->day_tdag.begin() + index);
    if (leaf == root_ && leaf->keys.empty()) {
        root_ = nullptr;
        return true;
    }
    if (leaf->keys.size() < (order_ + 1) / 2 - 1 && leaf != root_) {
        auto parent = leaf->parent;
        int parent_index = 0;
        for (size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i] == leaf) {
                parent_index = i;
                break;
            }
        }
        borrowOrMerge(leaf, parent, parent_index);
    }
    return true;
}


void BPlusTdag::removeInternal(long long day_ts, std::shared_ptr<BPlusTdagNode> node) {
    (void)day_ts; (void)node;
}

// Borrow or merge nodes during removal
void BPlusTdag::borrowOrMerge(std::shared_ptr<BPlusTdagNode> node, std::shared_ptr<BPlusTdagNode> parent, int index) {
    if (index > 0 && parent->children[index - 1]->keys.size() > (order_ + 1) / 2 - 1) {
        auto left_sibling = parent->children[index - 1];
        if (node->isLeaf) {
            node->keys.insert(node->keys.begin(), left_sibling->keys.back());
            node->day_tdag.insert(node->day_tdag.begin(), left_sibling->day_tdag.back());
            left_sibling->keys.pop_back();
            left_sibling->day_tdag.pop_back();
            parent->keys[index - 1] = node->keys.front();
        } else {
            node->keys.insert(node->keys.begin(), parent->keys[index - 1]);
            node->children.insert(node->children.begin(), left_sibling->children.back());
            //TODO:
            left_sibling->children.back()->parent = node;
            left_sibling->children.pop_back();
            parent->keys[index - 1] = left_sibling->keys.back();
            left_sibling->keys.pop_back();
        }
    } else if (index < static_cast<int>(parent->children.size()) - 1 && parent->children[index + 1]->keys.size() > (order_ + 1) / 2 - 1) {
        auto right_sibling = parent->children[index + 1];
        if (node->isLeaf) {
            node->keys.push_back(right_sibling->keys.front());
            node->day_tdag.push_back(right_sibling->day_tdag.front());
            right_sibling->keys.erase(right_sibling->keys.begin());
            right_sibling->day_tdag.erase(right_sibling->day_tdag.begin());
            parent->keys[index] = right_sibling->keys.empty() ? node->keys.back() : right_sibling->keys.front();
        } else {
            node->keys.push_back(parent->keys[index]);
            node->children.push_back(right_sibling->children.front());
            //TODO:
            right_sibling->children.front()->parent = node;
            right_sibling->children.erase(right_sibling->children.begin());
            parent->keys[index] = right_sibling->keys.front();
            right_sibling->keys.erase(right_sibling->keys.begin());
        }
    } else {
        if (index > 0) {
            auto left_sibling = parent->children[index - 1];
            if (node->isLeaf) {
                left_sibling->keys.insert(left_sibling->keys.end(), node->keys.begin(), node->keys.end());
                left_sibling->day_tdag.insert(left_sibling->day_tdag.end(), node->day_tdag.begin(), node->day_tdag.end());
                left_sibling->next = node->next;
                if (node->next) {
                    node->next->parent = left_sibling;
                }
            } else {
                left_sibling->keys.push_back(parent->keys[index - 1]);
                left_sibling->keys.insert(left_sibling->keys.end(), node->keys.begin(), node->keys.end());
                left_sibling->children.insert(left_sibling->children.end(), node->children.begin(), node->children.end());
                //TODO:
                for (auto& child : node->children) {
                    child->parent = left_sibling;
                }
            }
            // Remove the node from parent
            parent->keys.erase(parent->keys.begin() + index - 1);
            parent->children.erase(parent->children.begin() + index);
        } else {
            auto right_sibling = parent->children[index + 1];
            if (node->isLeaf) {
                node->keys.insert(node->keys.end(), right_sibling->keys.begin(), right_sibling->keys.end());
                node->day_tdag.insert(node->day_tdag.end(), right_sibling->day_tdag.begin(), right_sibling->day_tdag.end());
                node->next = right_sibling->next;
                if (right_sibling->next) {
                    right_sibling->next->parent = node;
                }
            } else {
                node->keys.push_back(parent->keys[index]);
                node->keys.insert(node->keys.end(), right_sibling->keys.begin(), right_sibling->keys.end());
                node->children.insert(node->children.end(), right_sibling->children.begin(), right_sibling->children.end());
                //TODO:
                for(auto& child : right_sibling->children) {
                    child->parent = node;
                }
            }
            parent->keys.erase(parent->keys.begin() + index);
            parent->children.erase(parent->children.begin() + index + 1);
        }
        if (parent == root_ && parent->keys.empty()) {
            root_ = node->isLeaf ? node : parent->children[0];
            //TODO:
            root_->parent = nullptr; // Update root's parent to null
        }
    }
}

// Query time candidates
std::vector<TimeCandidate>
BPlusTdag::query_time_candidates(const std::string& start_time, const std::string& end_time, 
                                 double lat_min, double lat_max, double lon_min, double lon_max) const {
    std::vector<TimeCandidate> result;
    // long long start_ts = TimeUtil::date_to_timestamp(start_time.substr(0, 10));
    // long long end_ts = TimeUtil::date_to_timestamp(end_time.substr(0, 10)) + 86399;
    // 以“天 00:00:00”为键；扫描上界仍用 +86399
    long long start_day = TimeUtil::date_to_timestamp(start_time.substr(0, 10));
    long long end_day   = TimeUtil::date_to_timestamp(end_time.substr(0, 10));
    long long start_ts  = start_day;
    long long end_ts    = end_day + 86399;
    int start_interval = TimeUtil::time_to_10min_interval(start_time);
    int end_interval = TimeUtil::time_to_10min_interval(end_time);

    DEBUG_LOG("Query start_ts: " << start_ts << ", end_ts: " << end_ts << 
              ", start_interval: " << start_interval << ", end_interval: " << end_interval);
    
    std::vector<double> query_min = {lat_min, lon_min};
    std::vector<double> query_max = {lat_max, lon_max};
    auto query_codes = ccs->generateQueryCubeCodes(query_min, query_max);

    auto days = rangeSearch(start_ts, end_ts);

    for (const auto& p : days) {
        long long day = p.first;
        auto tdag = p.second;
        if (!tdag) continue;
        // int sh = (day == start_ts) ? start_interval : 0;
        // int eh = (day == end_ts) ? end_interval : 143;
        int sh = (day == start_day) ? start_interval : 0;
        int eh = (day == end_day) ? end_interval : 143;
        
        auto cover = tdag->get_single_range_cover({sh, eh}, query_codes);
        if (cover.first != -1) {
            result.push_back({day, cover.first, cover.second});
        }
    }
    return result;
}