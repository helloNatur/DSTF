#ifndef SETUP_JXTP_H
#define SETUP_JXTP_H

#include <vector>
#include <map>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "Bloom.hpp"
#include "tool.hpp"
// #include "SegmentTree.h"
#include "bplus_tree.h"
#include <memory>  // 添加memory头文件以使用std::shared_ptr
#include <optional>
#include "TimeUtil.h"

class Setup_JXTp {
public:
    struct VectorHash {
        std::size_t operator()(const std::vector<unsigned char>& v) const {
            std::size_t seed = 0;
            for (const auto& byte : v) {
                seed ^= std::hash<unsigned char>{}(byte) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

private:
    inline static const std::string_view K_aes = "8975924566f6e252";
    inline static const std::string_view K_token = "89b7a92966f6eb32";
    inline static const std::string_view K_w = "7975922666f6eb02";
    inline static const std::string_view K_z = "9862192ad6f6ef65";
    inline static const std::string_view K_h = "9874a22554e7db85";

    int table_id;
    int key_column;
    int join_column;
    int record_num;
    std::size_t limit_n;
    std::string condition;
    std::vector<std::string> id;
    std::vector<std::vector<std::string>> keyword;
    std::vector<std::vector<std::string>> join_attr;

    std::unordered_map<std::string, std::vector<int>> reverse_id;
    std::optional<Bloom> f;//std::optional 默认构造为未初始化状态
    // Bloom f;//是Setu_JXTp的成员变量
    std::unordered_map<std::vector<unsigned char>, std::vector<std::vector<unsigned char>>,VectorHash> tset;
    std::unordered_map<long, std::vector<std::vector<unsigned char>>> cset;
    // std::shared_ptr<SegmentTree> segment_tree; // 添加 segment_tree 成员变量
    std::shared_ptr<BPlusTree> bplus_tree;

    static std::string toBase64(const std::vector<unsigned char>& bytes); 

public:
    Setup_JXTp(int table_id_, int key_column_num, int join_column_num, int record, 
                std::string condition_t,std::size_t limit_n);
    void construct();
    void saveToJson(const std::string& filename) const;
    // 返回可修改的引用
    std::unordered_map<std::vector<unsigned char>,std::vector<std::vector<unsigned char>>,VectorHash>& getTset() {
        return tset;
    }

    // 如果你在 const 对象上也需要访问，就再加一个 const 版本
    const std::unordered_map<std::vector<unsigned char>,std::vector<std::vector<unsigned char>>,VectorHash>& getTset() const {
        return tset;
    }
    [[nodiscard]] Bloom getF() const { return *f; }
    [[nodiscard]] auto getCset() const -> std::unordered_map<long, std::vector<std::vector<unsigned char>>> { return cset; }
    void store(std::string_view text) const;

    // std::shared_ptr<SegmentTree> getSegmentTree() const {
    //     return segment_tree;
    // }

    std::shared_ptr<BPlusTree> getBPlusTree() const {
        return bplus_tree;
    }

    // long long date_to_timestamp(const std::string& date);

    // int time_to_10min_interval(const std::string& time);

    // std::unique_ptr<SegmentTree> getSegmentTree() { return std::move(segment_tree); }
    // const SegmentTree* getSegmentTreePtr() const { return segment_tree.get(); }

    // [[nodiscard]] std::vector<std::shared_ptr<std::vector<unsigned char>>> queryTree(int l,int r) const {
    //     return segment_tree->query(l,r);
    // }

    // [[nodiscard]] std::vector<SegmentTree::IntervalResult> getCandidateIntervals(int l, int r, std::string keyword) const {
    //     return segment_tree->getCandidateIntervals(l, r, keyword);
    // }
    std::vector<SegmentTree::IntervalResult> getCandidateIntervals(const std::string& start_time,
                                                                  const std::string& end_time,
                                                                  double lat_min,double lat_max,
                                                                  double lon_min,double lon_max
                                                                  ) const {
        return bplus_tree->query_sql(start_time,end_time,lat_min,lat_max,lon_min,lon_max);
    }
};

#endif