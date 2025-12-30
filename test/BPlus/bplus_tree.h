#ifndef B_PLUS_TREE_H
#define B_PLUS_TREE_H

#include <climits>
#include <fstream>
#include <iostream>
#include <sstream>
#include "SegmentTree.h"
#include "cube_code.h"
#include "TimeUtil.h"

//B plus Tree implementation in C++,要求具有增删改查的能力
//优化线段树以存储小时级的时间范围，并确保 B+ 树支持动态插入新日期的线段树

class BPlusTreeNode; //前向声明

class BPlusTree {
private:
    static const int M = 3; //每个节点的最大键数
    
    std::shared_ptr<BPlusTreeNode> root; //根节点
    std::shared_ptr<CubeCode> ccs; // CubeCode对象，用于空间查询

    //辅助函数
    void insertInternal(long long key, std::shared_ptr<SegmentTree> segment_tree, std::shared_ptr<BPlusTreeNode> node);
    void splitNode(std::shared_ptr<BPlusTreeNode> node, std::shared_ptr<BPlusTreeNode> parent);
    std::shared_ptr<BPlusTreeNode> findLeafNode(long long key) const;
    void removeInternal(long long key, std::shared_ptr<BPlusTreeNode> node);
    void borrowOrMerge(std::shared_ptr<BPlusTreeNode> node, std::shared_ptr<BPlusTreeNode> parent, int index);


public:
    // BPlusTree(int d = 2,
    //           const std::vector<double>& min_vals={40.5, -74.2},
    //           const std::vector<double>& max_vals={41.0, -73.7},
    //           int levels=4);
    BPlusTree(int d ,
              const std::vector<double>& min_vals,
              const std::vector<double>& max_vals,
              int levels);

    // BPlusTree的基本操作增删改查
    void display();
    void insert(long long key, std::shared_ptr<SegmentTree> segment_tree);
    bool remove(long long key);
    bool update(long long key, int time_start,int time_end, 
                const std::shared_ptr<std::vector<unsigned char>>& token, 
                const std::vector<std::string>& keywords);

    std::shared_ptr<SegmentTree> search(long long key) const;

    std::vector<std::pair<long long, std::shared_ptr<SegmentTree>>> 
    rangeSearch(long long start, long long end) const;

    std::vector<SegmentTree::IntervalResult> query_sql(
        const std::string& start_time, const std::string& end_time, 
        double lat_min, double lat_max, double lon_min, double lon_max);

    //getter方法
    std::shared_ptr<CubeCode> getCubeCode() const { return ccs;}
};

class BPlusTreeNode {
public:
    bool isLeaf;
    std::vector<long long> keys; // 时间戳（精确到天）
    std::vector<std::shared_ptr<SegmentTree>> segment_trees; //线段树数组(仅叶子结点使用)
    std::vector<std::shared_ptr<BPlusTreeNode>> children; //子节点指针数组
    std::shared_ptr<BPlusTreeNode> next; //指向下一个叶子节点的指针（仅叶子结点使用）
    std::shared_ptr<BPlusTreeNode> parent; //指向父节点的指针
    

    BPlusTreeNode(bool leaf=false):isLeaf(leaf) {}

    int findKeyIndex(long long key) const {//使用二分查找在 keys 中查找第一个 ≥ key 的元素
        auto it = std::lower_bound(keys.begin(), keys.end(), key);//用于在 BPlusTreeNode 的 keys 向量中查找给定键 key 的插入位置或存在的索引
        return it - keys.begin();
    }
};

#endif