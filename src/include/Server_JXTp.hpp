#ifndef SERVER_JXTP_H
#define SERVER_JXTP_H

#include <vector>
#include <map>
#include "Bloom.hpp"
#include "Setup_JXTp.hpp"
#include "SegmentTree.h"
#include <unordered_map>
#include <memory> // 添加 memory 头文件

class Server_JXTp {
private:
    using VectorHash = Setup_JXTp::VectorHash;
    std::unordered_map<std::vector<unsigned char>, std::vector<std::vector<unsigned char>>, VectorHash> tset;
    std::optional<Bloom> f_1, f_2;
    std::unordered_map<long, std::vector<std::vector<unsigned char>>> cset1, cset2;
    std::vector<unsigned char> stag1;

    // std::shared_ptr<SegmentTree> segment_tree1, segment_tree2;

public:
    struct SearchResult {
        std::vector<std::vector<std::vector<unsigned char>>> res_range;
        std::vector<std::vector<std::vector<unsigned char>>> res_stag1;
    };
    
    Server_JXTp(const std::unordered_map<std::vector<unsigned char>, std::vector<std::vector<unsigned char>>, VectorHash>& tset_,
                Bloom f_1_, std::unordered_map<long, std::vector<std::vector<unsigned char>>> cset1_,
                Bloom f_2_, std::unordered_map<long, std::vector<std::vector<unsigned char>>> cset2_);

    [[nodiscard]] int tset_table1_cnt(const std::vector<unsigned char>& stag1_);

    auto search(std::vector<std::vector<std::vector<unsigned char>>> join_token01,
                std::vector<std::vector<std::vector<unsigned char>>> join_token02,
                std::vector<SegmentTree::IntervalResult>& id_tokens1,
                const std::vector<std::vector<std::vector<unsigned char>>>& stokens,
                const std::vector<std::vector<std::vector<unsigned char>>>& xtokens,
                const std::vector<std::vector<unsigned char>>& stags1) const-> SearchResult;

    
    // void set_stag2(const std::vector<unsigned char>& stag2_);

    // [[nodiscard]] std::vector<std::vector<unsigned char>> queryTree(int table_id,int l,int r) const {
    //     if (table_id == 1){
    //         return segment_tree1->query(l,r);
    //     }else if(table_id == 2){
    //         return segment_tree2->query(l,r);
    //     }else{
    //         throw std::runtime_error("Invalid table_id: " + std::to_string(table_id)); 
    //     }
    // }

    // // 添加公共方法以访问 segment_tree
    // [[nodiscard]] std::vector<std::vector<unsigned char>> queryTree1(int l, int r) const {
    //     return queryTree(1,l,r);
    // }

    // [[nodiscard]] std::vector<std::vector<unsigned char>> queryTree2(int l, int r) const {
    //     return queryTree(2,l,r);
    // }  
    
};

#endif