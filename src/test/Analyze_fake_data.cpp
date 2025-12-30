// 分析数据集中，每个10分钟区间内，包含了多少条记录，以及多少个唯一的Cube Code
// 以及哪些区间中包含特定css编码，这些区间的数量是？做测试看返回区间的数量的假阳性
#include <gtest/gtest.h>
#include "Setup_JXTp.hpp"
#include "Server_JXTp.hpp"
#include "cube_code.h"
#include "bplus_tree.h"
#include "Hash.hpp"
#include "AESUtil.hpp"
#include "tool.hpp"
#include <vector>
#include <string>
#include <set>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>
#include <map>
#include <unordered_set>  // 用于唯一Cube Code
#include <filesystem>

class JXTpTest : public ::testing::Test {
protected:
    void SetUp() override {
        K_token = "89b7a92966f6eb32";
        K_w = "7975922666f6eb02";
        K_h = "9874a22554e7db85";
        K_aes = "8975924566f6e252";

        key_column = 7;
        join_column = 1;
        record_num = 227428;
        condition = "";

        std::cout << "------------ JXT+ setup ------------\n";
        auto setup_start = std::chrono::high_resolution_clock::now();
        // 初始化 table_1 和 table_2
        table_1 = std::make_shared<Setup_JXTp>(1, key_column, join_column, record_num, condition);
        table_1->construct();
        auto& tset = table_1->getTset();
        table_2 = std::make_shared<Setup_JXTp>(2, key_column, join_column, record_num, condition);
        table_2->construct();
        tset.insert(table_2->getTset().begin(), table_2->getTset().end());

        join_attr1 = "userid";
        join_attr2 = "userid";
        start_time = "2012-04-03 18:00:00+00";
        end_time = "2012-04-08 10:10:10+00";
        lat_min = 40.7;
        lat_max = 40.76;
        lon_min = -74.0;
        lon_max = -73.98;
        keyword2 = "userid818"; 
        table_id = 1;
        path = std::string(DATA_DIR)+"/table" + std::to_string(table_id) + "/table" + std::to_string(table_id) +
                                 "_k" + std::to_string(key_column) + "_j" + std::to_string(join_column) +
                                 "_" + std::to_string(record_num) + condition + ".csv";


        //TODO:server只接收t1
        server = std::make_shared<Server_JXTp>(tset, table_1->getF(), table_1->getCset(), table_2->getF(), table_2->getCset());
        bplus_tree = table_1->getBPlusTree();
        auto setup_end = std::chrono::high_resolution_clock::now();
        auto setup_duration = std::chrono::duration_cast<std::chrono::milliseconds>(setup_end - setup_start);
        std::cout << "Setup time: " << setup_duration.count() << " ms\n";
    }

    std::filesystem::path path;
    std::string_view K_token, K_w, K_h, K_aes;
    int key_column, join_column, record_num,table_id;
    std::string condition;
    std::shared_ptr<Setup_JXTp> table_1;
    std::shared_ptr<Setup_JXTp> table_2;
    std::string_view join_attr1, join_attr2;
    std::string start_time, end_time;
    std::string_view keyword2;
    double lat_min, lat_max, lon_min, lon_max;
    std::shared_ptr<Server_JXTp> server;
    std::shared_ptr<BPlusTree> bplus_tree;
};

// 新增测试：分析每个10分钟区间的记录数和唯一Cube Code数
TEST_F(JXTpTest, Analyze10MinIntervals) {
    // ===================================================================================
    // 步骤 1: 扫描原始数据集，计算每个区间的真实统计数据（作为基准）
    // ===================================================================================
    std::cout << "\n--- 步骤 1: 正在从原始文件扫描和分析数据分布... ---\n";

    // 定义用于存储统计数据的结构
    struct IntervalStats {
        long long record_count = 0;
        std::unordered_set<std::string> unique_cube_codes;
    };
    // Key: {天的timestamp, 10分钟区间索引} -> Value: 统计数据
    std::map<std::pair<long long, int>, IntervalStats> analysis_map;
    // 存储真实匹配查询的区间ID
    std::set<std::pair<long long, int>> ground_truth_intervals;
    // 存储无key的真实匹配时间区间id
    std::set<std::pair<long long, int>> ground_truth_intervals_nokey;

    long long start_ts = TimeUtil::date_to_timestamp(start_time.substr(0, 10));
    long long end_ts = TimeUtil::date_to_timestamp(end_time.substr(0, 10)) + 86399; // 包含一天的秒数
    long long start_ts0 = TimeUtil::date_to_timestamp(start_time);
    long long end_ts0 = TimeUtil::date_to_timestamp(end_time) ; 
    int start_interval = TimeUtil::time_to_10min_interval(start_time);
    int end_interval = TimeUtil::time_to_10min_interval(end_time);


    auto ccs = table_1->getBPlusTree()->getCubeCode();
    std::ifstream file(path);
    ASSERT_TRUE(file.is_open()) << "无法打开数据集文件: " << path;

    std::string line;
    int counter = 0;
    // 解析数据
    while (std::getline(file, line)) {
        if (counter >= record_num + 1) {
            throw std::runtime_error("Too many records in CSV file");
        }
        if (line.empty()) continue;

        std::vector<std::string> record;
        size_t start=0,end;
        while((end=line.find(",",start))!=std::string::npos){
            record.push_back(line.substr(start,end-start));
            start=end+1;
        }
        record.push_back(line.substr(start));

        if(counter !=0){
            std::string time_str = record[7];//时间戳utctimestamp record[7]
            long long timestamp = TimeUtil::date_to_timestamp(time_str.substr(0, 10)); // 取前10位作为日期
            long long timestamp0 = TimeUtil::date_to_timestamp(time_str); 
            int interval = TimeUtil::time_to_10min_interval(time_str); //计算时间间隔索引0-143

            double lat = std::stod(record[4]);
            double lon = std::stod(record[5]);

            std::vector<double> point = {std::stod(record[4]), std::stod(record[5])}; // lat, lon
            // 计算区间ID
            auto interval_id = std::make_pair(timestamp, interval);

            // 1.1 更新全局统计数据
            analysis_map[interval_id].record_count++;
            auto codes = bplus_tree->getCubeCode()->generateDataCubeCodes(point);
            analysis_map[interval_id].unique_cube_codes.insert(codes.begin(), codes.end());

            // 1.2 检查记录是否完全符合查询条件（时空范围）
            if (timestamp0 >= start_ts0 && timestamp0 <= end_ts0 &&
                lat >= lat_min && lat <= lat_max && lon >= lon_min && lon <= lon_max) {
                ground_truth_intervals.insert(interval_id);
            }
            // 1.3 检查记录是否完全符合查询条件（时间范围）
            if (timestamp >= start_ts && timestamp <= end_ts) {
                ground_truth_intervals_nokey.insert(interval_id);
            }
        }
        ++counter;   
    }
    std::cout << "数据扫描完成。共分析了 " << analysis_map.size() << " 个非空时间区间。\n";

    // ===================================================================================
    // 步骤 2: 执行基于索引的查询，获取布隆过滤器返回的候选区间
    // ===================================================================================
    
    std::vector<double> query_min = {lat_min, lon_min};
    std::vector<double> query_max = {lat_max, lon_max};
    auto query_codes = table_1->getBPlusTree()->getCubeCode()->
                                generateQueryCubeCodes(query_min, query_max);
    std::vector<std::string> keyword1_list;
    for(const auto& code : query_codes) {
        // keyword1_list.push_back("spatial_code:" + code);
        keyword1_list.push_back(code);//都在第三层："40.718750,-73.981250,3"；"40.781250,-73.981250,3"
    }

    // 从B+树获取日期范围对应的段树
    auto trees = bplus_tree->rangeSearch(start_ts, end_ts);
    // 收集布隆过滤器返回的所有候选区间
    std::set<std::pair<long long, int>> indexed_result_intervals;
    for (const auto& [day_ts, st] : trees) {
        int sh = (day_ts == start_ts) ? start_interval : 0; // Start interval
        int eh = (day_ts == end_ts) ? end_interval : 143; // End interval (144 intervals in a day)
        
        auto candidates = st->getCandidateIntervals(sh, eh, query_codes);
        for (const auto& interval : candidates) {
            for (int i = interval.left; i <= interval.right; ++i) {
                indexed_result_intervals.insert({day_ts, i});
            }
        }
    }
    std::cout << "索引查询完成。\n";

    // ===================================================================================
    // 步骤 3: 分析并报告结果
    // ===================================================================================
    
    // 3.1 分析全局数据分布
    long long max_record_count = 0;
    size_t max_unique_codes = 0;
    for (const auto& pair : analysis_map) {
        if (pair.second.record_count > max_record_count) {
            max_record_count = pair.second.record_count;
        }
        if (pair.second.unique_cube_codes.size() > max_unique_codes) {
            max_unique_codes = pair.second.unique_cube_codes.size();
        }
    }

    std::cout << "\n====================== 分析报告 ======================\n";
    std::cout << "--- [A] 全局数据分布分析 ---\n";
    std::cout << "单个10分钟区间内的最大记录数: " << max_record_count << "\n";
    std::cout << "单个10分钟区间内的最大唯一Cube Code数: " << max_unique_codes << "\n";
    std::cout << "结论: 为避免BF饱和, SegmentTree的'capacity'应设为 >= " << max_unique_codes 
              << "。推荐值为 " << (size_t)(max_unique_codes * 1.5) << " (1.5倍安全余量)。\n";
    
    // 3.2 分析特定查询的假阳性率
    size_t ground_truth_count = ground_truth_intervals.size();
    size_t ground_truth_nokey_count = ground_truth_intervals_nokey.size();
    size_t indexed_result_count = indexed_result_intervals.size();
    size_t false_positives = (indexed_result_count > ground_truth_count) ? (indexed_result_count - ground_truth_count) : 0;
    double fp_rate = (indexed_result_count > 0) ? static_cast<double>(false_positives) / indexed_result_count : 0.0;

    std::cout << "\n--- [B] 查询假阳性分析 ---\n";
    std::cout << "查询时空范围: \n"
              << "  - 时间: [" << start_time << "] to [" << end_time << "]\n"
              << "  - 空间: Lat(" << lat_min << ", " << lat_max << "), Lon(" << lon_min << ", " << lon_max << ")\n";
    std::cout << "------------------------------------------------------\n";
    std::cout << std::left << std::setw(35) << "真实匹配区间数 (Ground Truth:nokey): " << ground_truth_nokey_count << "\n";
    std::cout << std::left << std::setw(35) << "真实匹配区间数 (Ground Truth):" << ground_truth_count << "\n";
    std::cout << std::left << std::setw(35) << "索引返回区间数 (Index Result):" << indexed_result_count << "\n";
    std::cout << std::left << std::setw(35) << "假阳性区间数 (False Positives):" << false_positives << "\n";
    std::cout << std::left << std::setw(35) << "假阳性率 (FP Rate):" 
              << std::fixed << std::setprecision(2) << (fp_rate * 100.0) << "%\n";
    std::cout << "======================================================\n";

    
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}