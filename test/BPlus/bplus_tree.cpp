#include "bplus_tree.h"
#include <algorithm>
#include <iostream>
#include <ctime>
#include <sstream>
#include <queue>
#include <memory>

#ifdef DEBUG
#define DEBUG_LOG(x) std::cout << x << std::endl
#else
#define DEBUG_LOG(x)
#endif

BPlusTree::BPlusTree(int d,
                     const std::vector<double>& min_vals,
                     const std::vector<double>& max_vals,
                     int levels) 
        : root(std::make_shared<BPlusTreeNode>(true)),
        ccs(std::make_shared<CubeCode>(d, min_vals, max_vals,levels)) {
}

// Insert a new day's SegmentTree
void BPlusTree::insert(long long key, std::shared_ptr<SegmentTree> segment_tree) {
    if (!root) {
        root = std::make_shared<BPlusTreeNode>(true);
        root->keys.push_back(key);
        root->segment_trees.push_back(segment_tree);
        // DEBUG_LOG("Inserted key" << key << "into new root" );
        return;
    }
    insertInternal(key, segment_tree, root);
}

// Internal insertion logic
void BPlusTree::insertInternal(long long key, std::shared_ptr<SegmentTree> segment_tree, std::shared_ptr<BPlusTreeNode> node) {
    if(!node) {
        DEBUG_LOG("Node is null, cannot insert key: " << key);
        return;
    }

    if (node->isLeaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        int index = static_cast<int>(it - node->keys.begin());

        //如果key已经存在，更新对应的SegmentTree
        if (it != node->keys.end() && *it == key) {
            node->segment_trees[index] = segment_tree; // Update existing day's SegmentTree
            // DEBUG_LOG("Updated key" << key << "in leaf node" );
            return;
        }

        //插入新的key和SegmentTree
        node->keys.insert(it, key);
        node->segment_trees.insert(node->segment_trees.begin() + index, segment_tree);
        // DEBUG_LOG("Inserted key" << key << "into leaf node with keys:" << [&node]() {
        //     std::stringstream ss;
        //     for (const auto& k : node->keys) {
        //         oss << k << " ";
        //     }
        //     return ss.str();
        // }());
        //若叶子结点溢出，执行分裂
        if (node->keys.size() >= M) {
            splitNode(node, node->parent);
        }
    } else {
        if (node->children.empty()) {
            std::cerr << "Error: Internal node has no children!" << std::endl;
            return;  // 或创建第一个 child
        }
        int index = node->findKeyIndex(key);

        // 关键修复：索引越界时修正为最后一个子节点
        if (index >= static_cast<int>(node->children.size())) {
            index = node->children.size()-1;
            // std::cerr << "ERROR: child index out of range in internal node. index = " << index
            //           << ", children.size() = " << node->children.size() << std::endl;
            // return;
        }

        auto child = node->children[index];
        insertInternal(key, segment_tree, child);
        if (child->keys.size() >= M) {
            splitNode(child, node);
        }
    }
}

// Split node when exceeding maximum keys
void BPlusTree::splitNode(std::shared_ptr<BPlusTreeNode> node, std::shared_ptr<BPlusTreeNode> parent) {
    auto new_node = std::make_shared<BPlusTreeNode>(node->isLeaf);
    int mid = node->keys.size()/ 2; //均匀分裂

    //把后一半的keys以及segment trees移动到新节点
    new_node->keys.assign(node->keys.begin() + mid, node->keys.end());
    if (node->isLeaf) {  //叶子节点，把后一半的segmenttree移动到新节点
        new_node->segment_trees.assign(node->segment_trees.begin() + mid, node->segment_trees.end());
        node->keys.resize(mid);
        node->segment_trees.resize(mid);
        new_node->next = node->next;
        new_node->parent = node->parent; // Maintain parent link
        // if (node->next) {
        //     node->next->parent = new_node;
        // }
        node->next = new_node;
    } else {//处理内部节点
        new_node->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
        new_node->children.assign(node->children.begin() + mid + 1, node->children.end());

        //调整原节点大小
        node->children.resize(mid+1);
        node->keys.resize(mid);

        //更新子节点的父指针
        for(auto& child : new_node->children) {
            child->parent = new_node; // Maintain parent link
        }
        
    }

    //上升的键：内部节点用中间键，叶子节点用新节点第一个键
    long long up_key = node->isLeaf ? new_node->keys[0] : node->keys[mid];
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

    //update parent
    if (!parent) {
        parent = std::make_shared<BPlusTreeNode>(false);
        root = parent;
        parent->keys.push_back(up_key);
        parent->children.push_back(node);
        parent->children.push_back(new_node);
        node->parent = parent;
        new_node->parent = parent;
        DEBUG_LOG( "Created new root with key: " << up_key );  
    } else {
        auto it = std::lower_bound(parent->keys.begin(), parent->keys.end(), up_key);
        int index = it - parent->keys.begin();

        parent->keys.insert(it, up_key);
        // parent->children[index] = node;
        parent->children.insert(parent->children.begin() + index + 1, new_node);

        node->parent = parent;
        new_node->parent = parent;

        //检查父节点是否需要分裂
        if(parent->keys.size()>=M){
            splitNode(parent,parent->parent);
        }
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

// Find leaf node for a given key
std::shared_ptr<BPlusTreeNode> BPlusTree::findLeafNode(long long key) const {
    auto node = root;
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

// Remove a day's SegmentTree
bool BPlusTree::remove(long long key) {
    auto leaf = findLeafNode(key);
    auto it = std::find(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end()) {
        std::cerr << "Key " << key << " not found" << std::endl;
        return false;
    }
    int index = it - leaf->keys.begin();
    leaf->keys.erase(it);
    leaf->segment_trees.erase(leaf->segment_trees.begin() + index);
    if (leaf == root && leaf->keys.empty()) {
        root = nullptr;
        return true;
    }
    if (leaf->keys.size() < (M + 1) / 2 - 1 && leaf != root) {
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


// Borrow or merge nodes during removal
void BPlusTree::borrowOrMerge(std::shared_ptr<BPlusTreeNode> node, std::shared_ptr<BPlusTreeNode> parent, int index) {
    if (index > 0 && parent->children[index - 1]->keys.size() > (M + 1) / 2 - 1) {
        auto left_sibling = parent->children[index - 1];
        if (node->isLeaf) {
            node->keys.insert(node->keys.begin(), left_sibling->keys.back());
            node->segment_trees.insert(node->segment_trees.begin(), left_sibling->segment_trees.back());
            left_sibling->keys.pop_back();
            left_sibling->segment_trees.pop_back();
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
    } else if (index < static_cast<int>(parent->children.size()) - 1 && parent->children[index + 1]->keys.size() > (M + 1) / 2 - 1) {
        auto right_sibling = parent->children[index + 1];
        if (node->isLeaf) {
            node->keys.push_back(right_sibling->keys.front());
            node->segment_trees.push_back(right_sibling->segment_trees.front());
            right_sibling->keys.erase(right_sibling->keys.begin());
            right_sibling->segment_trees.erase(right_sibling->segment_trees.begin());
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
                left_sibling->segment_trees.insert(left_sibling->segment_trees.end(), node->segment_trees.begin(), node->segment_trees.end());
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
                node->segment_trees.insert(node->segment_trees.end(), right_sibling->segment_trees.begin(), right_sibling->segment_trees.end());
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
        if (parent == root && parent->keys.empty()) {
            root = node->isLeaf ? node : parent->children[0];
            //TODO:
            root->parent = nullptr; // Update root's parent to null
        }
    }
}

// Update time range in SegmentTree
bool BPlusTree::update(long long key, int time_start, int time_end, 
                        const std::shared_ptr<std::vector<unsigned char>>& token, 
                        const std::vector<std::string>& keywords) {

    auto leaf = findLeafNode(key);
    int index = leaf->findKeyIndex(key);
    if (index >= static_cast<int>(leaf->keys.size()) || leaf->keys[index] != key) {
        std::cerr << "Key " << key << " not found for update" << std::endl;
        return false;
    }
    if (leaf->segment_trees[index]) {
        for (int i = time_start; i <= time_end; ++i) {
            leaf->segment_trees[index]->update(i, token, keywords);
        }
        return true;
    }
    return false;
}

// Search for a day's SegmentTree
std::shared_ptr<SegmentTree> BPlusTree::search(long long key) const {
    auto leaf = findLeafNode(key);
    if(!leaf) return nullptr;

    //在叶子结点中使用二分查找
    auto it = std::lower_bound(leaf->keys.begin(),leaf->keys.end(),key);
    if(it != leaf->keys.end() && *it == key){
        int index = it-leaf->keys.begin();
        return leaf->segment_trees[index];
    }
    // int index = leaf->findKeyIndex(key);
    // if (index < static_cast<int>(leaf->keys.size()) && leaf->keys[index] == key) {
    //     return leaf->segment_trees[index];
    // }
    return nullptr;
}

// Range search for SegmentTrees
std::vector<std::pair<long long, std::shared_ptr<SegmentTree>>> BPlusTree::rangeSearch(long long start_key, long long end_key) const {
    std::vector<std::pair<long long, std::shared_ptr<SegmentTree>>> result;
    auto leaf = findLeafNode(start_key);
    while (leaf && !leaf->keys.empty() && leaf->keys[0] <= end_key) {
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] >= start_key && leaf->keys[i] <= end_key) {
                result.emplace_back(leaf->keys[i], leaf->segment_trees[i]);
            }
        }
        leaf = leaf->next;
    }
    return result;
}

// SQL query implementation
// 输入：模拟的sql语句
// 输出：符合条件的tokens
std::vector<SegmentTree::IntervalResult> BPlusTree::query_sql(
    const std::string& start_time, const std::string& end_time, 
    double lat_min, double lat_max, double lon_min, double lon_max) {
    std::vector<SegmentTree::IntervalResult> result;
    // struct tm tm_start = {}, tm_end = {};
    // strptime(start_time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm_start);
    // strptime(end_time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm_end);
    // time_t start_t = timegm(&tm_start);
    // time_t end_t = timegm(&tm_end);
    // long long start_ts = start_t - (start_t % 86400);
    // long long end_ts = end_t - (end_t % 86400) + 86399;
    // //TODO:
    // int start_interval = tm_start.tm_hour*6+tm_start.tm_min/10; // 10-minute intervals
    // int end_interval = tm_end.tm_hour*6+tm_end.tm_min/10;
    
    // 使用 TimeUtil 进行时间转换
    long long start_ts = TimeUtil::date_to_timestamp(start_time.substr(0, 10));
    long long end_ts = TimeUtil::date_to_timestamp(end_time.substr(0, 10)) + 86399; // 包含一天的秒数
    int start_interval = TimeUtil::time_to_10min_interval(start_time);
    int end_interval = TimeUtil::time_to_10min_interval(end_time);

    DEBUG_LOG("Query start_ts: " << start_ts << ", end_ts: " << end_ts << 
        ", start_interval: " << start_interval << ", end_interval: " << end_interval);
    
    //TODO:这里keyword应该为空间2维转为一维
    std::vector<double> query_min = {lat_min, lon_min};
    std::vector<double> query_max = {lat_max, lon_max};
    auto query_codes = ccs->generateQueryCubeCodes(query_min, query_max);

    auto trees = rangeSearch(start_ts, end_ts);
    for (const auto& [key, st] : trees) {
        int sh = (key == start_ts) ? start_interval : 0; // Start interval
        int eh = (key == end_ts) ? end_interval : 143; // End interval (144 intervals in a day)
        
        auto candidates = st->getCandidateIntervals(sh, eh, query_codes);
        for (const auto& interval : candidates) {
            // result.push_back(interval);
            result.emplace_back(interval.left,interval.right,key,interval.tokens);
        }
    }
    
    return result;
}

void BPlusTree::display() {
    if (!root) return;
    std::queue<std::shared_ptr<BPlusTreeNode>> q;
    q.push(root);
    int level = 0;
    while (!q.empty()) {
        int size = q.size();
        while (size--) {
            auto node = q.front();
            q.pop();
            std::cout << (node->isLeaf ? "Leaf" : "Internal") << " node keys: ";
            for (const auto& k : node->keys) std::cout << k << " ";
            std::cout << std::endl;
            if (!node->isLeaf) {
                for (const auto& child : node->children) {
                    q.push(child);
                }
            }
        }
        std::cout << "----" << std::endl;
        ++level;
    }
}
