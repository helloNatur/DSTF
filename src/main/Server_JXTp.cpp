#include "Server_JXTp.hpp"
#include "tool.hpp"
#include "SegmentTree.h"
#include <algorithm>
#include <iostream>

Server_JXTp::Server_JXTp(const std::unordered_map<std::vector<unsigned char>, std::vector<std::vector<unsigned char>>, VectorHash>& tset_,
                         Bloom f_1_, std::unordered_map<long, std::vector<std::vector<unsigned char>>> cset1_,
                         Bloom f_2_, std::unordered_map<long, std::vector<std::vector<unsigned char>>> cset2_)
    : tset(tset_), f_1(std::move(f_1_)), cset1(std::move(cset1_)), 
      f_2(std::move(f_2_)), cset2(std::move(cset2_)) {}

int Server_JXTp::tset_table1_cnt(const std::vector<unsigned char>& stag1_) {
    if (tset.find(stag1_) != tset.end()) {
        return tset.at(stag1_).size();
    }
    return 0;
}

// auto Server_JXTp::search(std::vector<std::vector<std::vector<unsigned char>>> join_token01,
//                         const std::vector<std::vector<unsigned char>>& id_tokens1,
//                         const std::vector<std::vector<unsigned char>>& stoken,
//                         const std::vector<std::vector<unsigned char>>& xtoken) const
//     -> std::vector<std::vector<std::vector<unsigned char>>> {
//     std::vector<std::vector<std::vector<unsigned char>>> res;
//     //多个keyword
//     for (size_t idx = 0; idx < join_token01.size(); ++idx) {
//         // 获取对应的 id token
//         if (idx >= id_tokens1.size()) {
//             continue; // 防止越界
//         }
//         const auto& id_token = id_tokens1[idx];
//         const auto& id_token1_tset = tset.at(id_token);
//         for(size_t i=0;i<id_token1_tset.size();++i){
//             auto join_token0_t=tool::Xor(join_token01[idx][i],id_token1_tset[i]);
//             long join_token0_long = tool::bytesToLong(join_token0_t);
//             if (f_2->mayContain(join_token0_long)) {
//                 auto stoken_t = tool::Xor(stoken[i], id_token1_tset[i]);
//                 res.push_back(cset1.at(tool::bytesToLong(stoken_t)));
//                 res.push_back(cset2.at(join_token0_long));
//             }
//         }
//     }

//     const auto& token1_tset = tset.at(stag1);
//     for (size_t i = 0; i < token1_tset.size(); ++i) {
//         auto xtoken_t = tool::Xor(xtoken[i], token1_tset[i]);
//         long xtoken_long = tool::bytesToLong(xtoken_t);
//         if (f_2->mayContain(xtoken_long)) {
//             auto stoken_t = tool::Xor(stoken[i], token1_tset[i]);
//             res.push_back(cset1.at(tool::bytesToLong(stoken_t)));
//             res.push_back(cset2.at(xtoken_long));
//         }
//     }
//     return res;
// }

auto Server_JXTp::search(std::vector<std::vector<std::vector<unsigned char>>> join_token01,
                        std::vector<std::vector<std::vector<unsigned char>>> join_token02,
                        std::vector<SegmentTree::IntervalResult>& id_tokens1,
                        const std::vector<std::vector<std::vector<unsigned char>>>& stokens,
                        const std::vector<std::vector<std::vector<unsigned char>>>& xtokens,
                        const std::vector<std::vector<unsigned char>>& stags1) const
    -> SearchResult {
    // std::vector<std::vector<std::vector<unsigned char>>> res_range; // 范围查询结果
    // std::vector<std::vector<std::vector<unsigned char>>> res_stag1; // stag1 查询结果
    //std::vector<std::vector<std::vector<unsigned char>>> res;
    SearchResult result;
    result.res_range.reserve(join_token01.size()*2); // 预分配空间
    result.res_stag1.reserve(xtokens.size()*10);     // 预分配空间
    // std::cout << "join-token0 size: " << join_token01.size()<<"\n";
    // std::cout << "result.res_range size: " << result.res_range.size()<<"\n";

    // 处理范围查询的 join_token01
    for (size_t idx = 0; idx < join_token01.size(); ++idx) {
        if (idx >= id_tokens1.size()) {
            continue; // 防止越界
        }

        const auto& interval = id_tokens1[idx]; // 获取当前区间
        for (const auto& id_token : interval.tokens) { // 迭代 tokens 中的每个 token
        // const auto& id_token = id_tokens1[idx];
            auto it = tset.find(*id_token);//存在一个返回结果
            if (it != tset.end()) {
                const auto& id_token1_tset = it->second;
                for (size_t i = 0; i < id_token1_tset.size() && i < join_token01[idx].size()&&i<join_token02[idx].size(); ++i) {//没有进入循环
                //for(size_t i = 0;i<id_token1_tset.size();++i){
                    auto join_token02_t = tool::Xor(join_token02[idx][i], id_token1_tset[i]);
                    long join_token02_long = tool::bytesToLong(join_token02_t);
                    if (f_2->mayContain(join_token02_long)) {//这里进不去
                        auto join_token01_t = tool::Xor(join_token01[idx][i], id_token1_tset[i]);
                        long join_token01_long = tool::bytesToLong(join_token01_t);
                        // 仅当 join-attr0 匹配时添加
                        if (cset1.find(join_token01_long) != cset1.end() && cset2.find(join_token02_long) != cset2.end()) {
                            result.res_range.push_back(cset1.at(join_token01_long));
                            result.res_range.push_back(cset2.at(join_token02_long));
                        }
                        
                        // if (cset1.find(join_token01_long ) != cset1.end()) {//这里只有一个？为什么
                        //     result.res_range.push_back(cset1.at(join_token01_long ));
                        // }
                        // if (cset2.find(join_token02_long) != cset2.end()) {
                        //     //std::cout << "cset2 found xtoken_long at idx " << idx << ", i " << i << std::endl;
                        //     result.res_range.push_back(cset2.at(join_token02_long));
                        // }
                    }
                }
            } else {
            std::cerr << "Warning: id_token not found in tset at index " << idx << std::endl;
            }
        }
    }
    //std::cout << "result.res_range size: " << result.res_range.size()<<"\n";//result.res_range size应该为230

    // 处理 stag1 相关的查询
    // TODO: 这里stag1传值存在问题，需要修改
    // static const std::string_view K_token = "89b7a92966f6eb32";
    // std::string_view keyword1 = "keyword0table1_keyword_0_0";
    // std::string_view join_attr1 = "join-attr0";

    // std::string stag_input1 = std::string{K_token} + std::string{keyword1} + std::string{join_attr1} + "1";
    // auto stag1 = Hash::Get_SHA_256(stag_input1);
    for(size_t s_idx = 0;s_idx <stags1.size();++s_idx){
        const auto& stag1=stags1[s_idx];
        const auto& xtoken=xtokens[s_idx];
        const auto& stoken=stokens[s_idx];
        
        auto it = tset.find(stag1);
        if (it != tset.end()) {
            const auto& token1_tset = it->second;
            if (!token1_tset.empty()) {
                for (size_t i = 0; i < token1_tset.size() && i < xtoken.size() && i < stoken.size(); ++i) {
                    auto xtoken_t = tool::Xor(xtoken[i], token1_tset[i]);
                    if (xtoken_t.empty()) {
                        std::cerr << "Error: tool::Xor returned empty vector for xtoken_t at index " << i << std::endl;
                        continue;
                    }
                    long xtoken_long = tool::bytesToLong(xtoken_t);
                    if (xtoken_long == 0 && !xtoken_t.empty()) { // 简单验证 bytesToLong 是否合理
                        std::cerr << "Warning: tool::bytesToLong returned zero for non-empty xtoken_t at index " << i << std::endl;
                    }
                    if (f_2 && f_2->mayContain(xtoken_long)) {
                        auto stoken_t = tool::Xor(stoken[i], token1_tset[i]);
                        if (stoken_t.empty()) {
                            std::cerr << "Error: tool::Xor returned empty vector for stoken_t at index " << i << std::endl;
                            continue;
                        }
                        long stoken_long = tool::bytesToLong(stoken_t);
                        auto cset1_it = cset1.find(stoken_long);
                        if (cset1_it != cset1.end()) {
                            result.res_stag1.push_back(cset1_it->second);
                        } else {
                            std::cerr << "Warning: stoken_long " << stoken_long << " not found in cset1 at index " << i << std::endl;
                        }
                        auto cset2_it = cset2.find(xtoken_long);
                        if (cset2_it != cset2.end()) {
                            result.res_stag1.push_back(cset2_it->second);
                        } else {
                            std::cerr << "Warning: xtoken_long " << xtoken_long << " not found in cset2 at index " << i << std::endl;
                        }
                    }
                }
            } else {
                std::cerr << "Warning: token1_tset is empty for stag1" << std::endl;
            }
        }
    }
    //  else {
    //     // std::cerr << "Error: stag1 not found in tset" << std::endl;
    // }

    // // 计算 res_range 和 res_stag1 的交集
    // if (res_range.empty() || res_stag1.empty()) {
    //     return res; // 如果任一结果为空，交集为空
    // }

    // // 对 res_range 和 res_stag1 排序，以便使用 std::set_intersection
    // // 定义比较函数
    // auto compare = [](const std::vector<std::vector<unsigned char>>& a, const std::vector<std::vector<unsigned char>>& b) {
    //     if (a.size() != b.size()) return a.size() < b.size();
    //     for (size_t i = 0; i < a.size(); ++i) {
    //         if (a[i].size() != b[i].size()) return a[i].size() < b[i].size();
    //         for (size_t j = 0; j < a[i].size(); ++j) {
    //             if (a[i][j] != b[i][j]) return a[i][j] < b[i][j];
    //         }
    //     }
    //     return false;
    // };

    // std::sort(res_range.begin(), res_range.end(), compare);
    // std::sort(res_stag1.begin(), res_stag1.end(), compare);

    // // 计算交集
    // std::set_intersection(res_range.begin(), res_range.end(),
    //                      res_stag1.begin(), res_stag1.end(),
    //                      std::back_inserter(res), compare);
    
    // res.reserve(res_range.size() + res_stag1.size()); // 预分配空间以提高效率

    // // 合并 res_range
    // res.insert(res.end(), res_range.begin(), res_range.end());

    // // 合并 res_stag1
    // res.insert(res.end(), res_stag1.begin(), res_stag1.end());

    return result;

}

// void Server_JXTp::set_stag2(const std::vector<unsigned char>& stag2_) {
//     stag2 = stag2_;
// }