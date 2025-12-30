#include "Setup_JXTp.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "Hash.hpp"
#include "AESUtil.hpp"
#include "tool.hpp"
#include "SegmentTree.h"
#include <nlohmann/json.hpp>
#include <iostream>



// Setup_JXTp.hpp
// 修改构造函数的初始化列表
//SegmentTree类的构造函数
Setup_JXTp::Setup_JXTp(int table_id_, int key_column_num, int join_column_num, int record, std::string condition_t)
    : table_id{table_id_}, key_column{key_column_num}, join_column{join_column_num}, record_num{record},
        condition{std::move(condition_t)},
        // segment_tree(std::make_shared<SegmentTree>(record, 0.01, 10)) {//记录数，bloom filter的误判率，bloom filter的容量
        bplus_tree(std::make_shared<BPlusTree>(2, std::vector<double>{40.5, -74.2}, std::vector<double>{41.0, -73.7}, 8)){} 
  
// //具体的年月日
// long long Setup_JXTp::date_to_timestamp(const std::string& date) {
//     struct tm tm = {};
//     strptime(date.c_str(), "%Y-%m-%d", &tm);
//     tm.tm_hour = 0;
//     tm.tm_min = 0;
//     tm.tm_sec = 0;
//     time_t t = timegm(&tm); // Use timegm to handle UTC conversion，timegm使用utc时间戳，mktime使用本地时间戳
//     return static_cast<long long>(t); // Convert to seconds since epoch
// }

// // Helper function to convert time string to 10-minute interval
// //把一个包含时区的完整时间字符串（格式为 "YYYY-MM-DD HH:MM:SS+ZZZZ"）转换为该时间在一天中的第几个 10 分钟区间。
// int Setup_JXTp::time_to_10min_interval(const std::string& time) {
//     struct tm tm = {};
//     strptime(time.c_str(), "%Y-%m-%d %H:%M:%S%z", &tm); // Support +00 timezone
//     return tm.tm_hour * 6 + tm.tm_min / 10; // 24 hours * 6 intervals per hour ep：18:20 → 18 小时 * 6 + 20 ÷ 10 = 108 + 2 = 110（第 110 个 10 分钟区间）
// }


void Setup_JXTp::construct() {
    id.resize(record_num + 1);
    keyword.resize(record_num + 1, std::vector<std::string>(key_column));
    join_attr.resize(record_num + 1, std::vector<std::string>(join_column));

    std::filesystem::path path = std::string(DATA_DIR)+"/table" + std::to_string(table_id) + "/table" + std::to_string(table_id) +
                                 "_k" + std::to_string(key_column) + "_j" + std::to_string(join_column) +
                                 "_" + std::to_string(record_num) + condition + ".csv";

    std::ifstream file{path};
    if (!file) throw std::runtime_error("Failed to open " + path.string());

    std::string line;
    int counter = 0;
    while (std::getline(file, line)) {
        if (counter >= record_num + 1) {
            throw std::runtime_error("Too many records in CSV file");
        }
        // 如果行末尾有回车符，就移除它
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        std::vector<std::string> record;
        size_t start=0,end;
        while((end=line.find(",",start))!=std::string::npos){
            record.push_back(line.substr(start,end-start));
            start=end+1;
        }
        record.push_back(line.substr(start));

        // id[counter] = record[0];
        // for (int j = 0; j < key_column; ++j) {
        //     keyword[counter][j] = record[j + 1];
        //     if (counter != 0) {
        //         std::string kword = keyword[0][j] + record[j + 1];
        //         reverse_id[kword].push_back(counter);
        //     }
        // }
        // for (int j = 0; j < join_column; ++j) {
        //     join_attr[counter][j] = record[key_column + j + 1];
        //     if (counter != 0) {
        //         std::string kword = join_attr[0][j] + record[key_column + j + 1];
        //         reverse_id[kword].push_back(counter);
        //     }
        // }
        // if (counter != 0) {
        //     std::string kword = "id" + record[0];
        //     std::string keyword = "keyword0" + record[1];
        //     reverse_id[kword].push_back(counter);
        //     std::string token_input = std::string{K_token} + kword + join_attr[0][0] + std::to_string(table_id);
        //     auto token = std::make_shared<std::vector<unsigned char>>(Hash::Get_SHA_256(token_input));//table1和table2的token是不一样的
        //     // std::cout << "Inserting token for ID: " << record[0] << std::endl;
        //     //TODO:这里的kw必须改为变量
        //     segment_tree->update(counter-1, token,keyword);//存在顺序的线段树
        // }
        id[counter] = record[8];
        // for (int j = 0; j < key_column-1; ++j) {
        //     // keyword[counter][j] = record[j + 1];
        //     keyword[counter][j] = record[j + 1];
        //     if (counter != 0) {
        //         std::string kword = keyword[0][j] + record[j + 1];
        //         reverse_id[kword].push_back(counter);
        //     }
        // }
        for (int j = 0; j < join_column; ++j) {
            join_attr[counter][j] = record[0];
            if (counter != 0) {
                std::string kword = join_attr[0][j] + record[0];
                reverse_id[kword].push_back(counter);
            }
        }
        // TODO：
        // 针对时间戳utctimestamp record[7]建立的倒排索引，以10min为粒度建立，形成10min与id的倒排
        // TODO：
        // 针对latitude-record[4],longitude-record[5]倒排索引的建立，感觉也很神奇
        // 经过ccs建立倒排索引，形成ccs编码与id的倒排
        if(counter != 0){
            std::string time_str = record[7];
            long long timestamp = TimeUtil::date_to_timestamp(time_str.substr(0, 10)); // 取前10位作为日期
            int interval = TimeUtil::time_to_10min_interval(time_str); //计算时间间隔索引0-143
            std::vector<double> point = {std::stod(record[4]), std::stod(record[5])}; // lat, lon

            // === 新增：时间戳倒排索引 ===
            std::string time_key = "utctimestamp" + std::to_string(timestamp)+ "_" + std::to_string(interval);
            reverse_id[time_key].push_back(counter);

            // === 新增：空间编码倒排索引 ===
            auto codes = bplus_tree->getCubeCode()->generateDataCubeCodes(std::vector<double>(point)); // lat, lon
            for (const auto& code : codes) {
                // std::string spatial_key = "spatial_code:" + code;
                std::string spatial_key = code;
                reverse_id[spatial_key].push_back(counter);
            }

            //获取或创建改日期的线段树
            auto segment_tree = bplus_tree->search(timestamp);
            if(!segment_tree) {
                segment_tree = std::make_shared<SegmentTree>(144, 0.001, 442); // 创建新的线段树
                bplus_tree->insert(timestamp, segment_tree);
            }

            //TODO:token为时间戳的加密值，keyword为空间2维转为1维的值
            std::string token_input = std::string{K_token} + time_key + join_attr[0][0] + std::to_string(table_id);
            // auto token = Hash::Get_SHA_256(token_input); // table1和table2的token是不一样的
            std::shared_ptr<std::vector<unsigned char>> token =
            std::make_shared<std::vector<unsigned char>>(Hash::Get_SHA_256(token_input));

            bplus_tree->update(timestamp, interval, interval,token, codes); // 更新线段树
            //st->update(interval, token, codes); // 更新线段树这里codes可以是vector，因为bf->add的时候是for codes
        }
        ++counter;
    }

    std::vector<std::vector<unsigned char>> join_hash(join_column);
    for (int j = 0; j < join_column; ++j) {
        join_hash[j] = Hash::Get_SHA_256(std::string{K_h} + join_attr[0][j]);
    }

    // 计算 xy 所需的总大小
    size_t total_xy_size = 0;
    for (const auto& [kword, reverse_tmp] : reverse_id) {
        total_xy_size += reverse_tmp.size() * join_column;
    }
    std::vector<long> xy(total_xy_size);
    int xy_counter = 0;

    // std::string input_buffer;
    // input_buffer.reserve(128); // 预分配足够的空间以减少 realloc 次数

    for (const auto& [kword, reverse_tmp] : reverse_id) {
        if (reverse_tmp.empty()) {
            std::cerr << "Warning: empty reverse_tmp for keyword: " << kword << std::endl;
            continue;
        }


        
        auto w = Hash::Get_SHA_256(std::string{K_w} + kword + "0");
        auto K_enc = Hash::Get_SHA_256(std::string{K_aes} + kword);
        // 在代码中，std::string{K_w} + kword + "0" 这样的表达式会创建多个临时 
        // std::string 对象，导致不必要的内存分配和拷贝。
        // input_buffer.assign(K_w);
        // input_buffer.append(kword);
        // input_buffer.append("0");
        // auto w = Hash::Get_SHA_256(input_buffer);

        // input_buffer.clear(); //重复使用buffer
        // input_buffer.append(K_aes);
        // input_buffer.append(kword);
        // auto K_enc = Hash::Get_SHA_256(input_buffer);

        std::vector<std::vector<std::vector<unsigned char>>> t(join_column);
        
        for (size_t i = 0; i < reverse_tmp.size(); ++i) {
            int record_id = reverse_tmp[i];

            if (record_id >= id.size() || id[record_id].empty()) {
                throw std::runtime_error("Invalid record_id or empty id: " + std::to_string(record_id));
            }

            auto w_cnt = Hash::Get_SHA_256(std::string{K_w} + kword + std::to_string(i + 1));
            // input_buffer.clear();
            // input_buffer.append(K_w);
            // input_buffer.append(kword);
            // input_buffer.append(std::to_string(i + 1));
            // auto w_cnt = Hash::Get_SHA_256(input_buffer);
            auto ct_tmp = AESUtil::encrypt(K_enc, id[record_id]);

            if (ct_tmp.empty()) {
                throw std::runtime_error("AES encryption failed for record_id: " + std::to_string(record_id));
            }

            for (int j = 0; j < join_column; ++j) {
                if (xy_counter >= xy.size()) {
                    throw std::runtime_error("xy_counter out of bounds: " + std::to_string(xy_counter));
                }

                auto y = Hash::Get_SHA_256(std::string{K_z} + join_attr[record_id][j]);
                auto tset_each = tool::Xor(w_cnt, y);
                // xy[xy_counter] = tool::bytesToLong(tool::Xor(tool::Xor(w, y), join_hash[j]));

                auto xor_result = tool::Xor(w, y);
                if (xor_result.empty()) {
                    throw std::runtime_error("tool::Xor(w, y) returned empty vector");
                }
                auto final_xor = tool::Xor(xor_result, join_hash[j]);
                if (final_xor.empty()) {
                    throw std::runtime_error("tool::Xor(xor_result, join_hash[j]) returned empty vector");
                }
                xy[xy_counter] = tool::bytesToLong(final_xor);
                if (auto [it, inserted] = cset.emplace(xy[xy_counter], std::vector<std::vector<unsigned char>>{}); !inserted) {
                    it->second.push_back(ct_tmp);
                    tset_each = tool::Xor(tset_each, std::vector<unsigned char>{K_z.begin(), K_z.end()});
                } else {
                    it->second.push_back(ct_tmp);
                }
                if (i == 0) t[j] = {};
                t[j].push_back(tset_each);
                ++xy_counter;
            }
        }
        for (int i = 0; i < join_column; ++i) {  
            auto token = Hash::Get_SHA_256(std::string{K_token} + kword + join_attr[0][i] + std::to_string(table_id));
            tset[token] = std::move(t[i]);
        }
    }
    f = Bloom::construct(xy, 64);
}

std::string Setup_JXTp::toBase64(const std::vector<unsigned char>& bytes) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    size_t i = 0;
    size_t j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (auto byte : bytes) {
        char_array_3[i++] = byte;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; j < i + 1; j++)
            result += base64_chars[char_array_4[j]];

        while (i++ < 3)
            result += '=';
    }

    return result;
}

void Setup_JXTp::store(std::string_view text) const {
    std::filesystem::path filepath = "data/EDB/JXT+_" + std::string{text} + ".dat";
    std::ofstream file{filepath, std::ios::binary};
    if (!file) throw std::runtime_error("Failed to open " + filepath.string());

    for (const auto& [token, tuples] : tset) {
        file.write(reinterpret_cast<const char*>(token.data()), token.size());
        for (const auto& t : tuples) {
            file.write(reinterpret_cast<const char*>(t.data()), t.size());
        }
    }
    for (const auto& x : f->getData()) {
        file.write(reinterpret_cast<const char*>(x.data()), x.size());
    }
    for (const auto& [key, ct] : cset) {
        file.write(reinterpret_cast<const char*>(&key), sizeof(key));
        for (const auto& c : ct) {
            file.write(reinterpret_cast<const char*>(c.data()), c.size());
        }
    }
}

void Setup_JXTp::saveToJson(const std::string& filename) const {
    using json = nlohmann::json;

    try{
        //handle reverse_id
        json j_reverse_id = reverse_id;
        std::ofstream reverse_id_file(std::string(DATA_DIR)+"/EDB/JXT+_reverse_id_" + filename + ".json");
        reverse_id_file << std::setw(4) << j_reverse_id << std::endl;
        reverse_id_file.close();

        //handle tset
        json j_tset;
        for (const auto& [token, tuples] : tset) {
            json j_tuples = json::array();
            for (const auto& t : tuples) {
                j_tuples.push_back(toBase64(t));
            }
            j_tset[toBase64(token)] = j_tuples;
        }
        std::ofstream tset_file(std::string(DATA_DIR)+"/EDB/JXT+_tset_" + filename + ".json");
        tset_file << std::setw(4) << j_tset << std::endl;
        tset_file.close();

        //handle cset
        json j_cset;
        for (const auto& [key, ct] : cset) {
            json j_ct = json::array();
            for (const auto& c : ct) {
                j_ct.push_back(toBase64(c));
            }
            j_cset[std::to_string(key)] = j_ct;
        }
        std::ofstream cset_file(std::string(DATA_DIR)+"/EDB/JXT+_cset_" + filename + ".json");
        cset_file << std::setw(4) << j_cset << std::endl;
        cset_file.close();

        //handle xset:bloom filter
        if(f.has_value()){
            json j_bloom = json::array();
            for (const auto& bytes : f->getData()) {
                j_bloom.push_back(toBase64(bytes));
            }
            std::ofstream bloom_file(std::string(DATA_DIR)+"/EDB/JXT+_xset_" + filename + ".json");
            bloom_file << std::setw(4) << j_bloom << std::endl;
            bloom_file.close();
        }
        std::cout << "JSON files saved for table " << table_id << ": " << filename << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving JSON - " << e.what() << std::endl;
    }
}
