#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct QueryPlanRow {
    std::string dataset;
    std::string baseline_type;
    int alpha = 1;
    int repeat = 1;
    std::string query_start;
    std::string query_end;
    double lat_min = 0.0;
    double lat_max = 0.0;
    double lon_min = 0.0;
    double lon_max = 0.0;
    double temporal_window_hours = 0.0;
    double lat_window_size = 0.0;
    double lon_window_size = 0.0;
    bool was_clamped = false;
    double actual_temporal_scaling_factor = 1.0;
};

inline std::string QpTrim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

inline std::vector<std::string> QpSplitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (ch == ',' && !in_quotes) {
            fields.push_back(QpTrim(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(QpTrim(current));
    return fields;
}

inline std::unordered_map<std::string, std::string> QpZipRow(
    const std::vector<std::string>& header,
    const std::vector<std::string>& fields) {
    std::unordered_map<std::string, std::string> row;
    for (std::size_t i = 0; i < header.size() && i < fields.size(); ++i) {
        row[header[i]] = fields[i];
    }
    return row;
}

inline std::string QpGet(const std::unordered_map<std::string, std::string>& row,
                         const std::string& key,
                         const std::string& fallback = "") {
    const auto it = row.find(key);
    if (it == row.end() || it->second.empty()) {
        return fallback;
    }
    return it->second;
}

inline double QpGetDouble(const std::unordered_map<std::string, std::string>& row,
                          const std::string& key,
                          double fallback = 0.0) {
    const std::string value = QpGet(row, key);
    if (value.empty()) {
        return fallback;
    }
    return std::stod(value);
}

inline int QpGetInt(const std::unordered_map<std::string, std::string>& row,
                    const std::string& key,
                    int fallback = 0) {
    const std::string value = QpGet(row, key);
    if (value.empty()) {
        return fallback;
    }
    return std::stoi(value);
}

inline bool QpGetBool(const std::unordered_map<std::string, std::string>& row,
                      const std::string& key,
                      bool fallback = false) {
    std::string value = QpGet(row, key);
    if (value.empty()) {
        return fallback;
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value == "1" || value == "true" || value == "yes";
}

inline std::vector<QueryPlanRow> LoadQueryPlanRows(const std::string& path,
                                                   const std::string& dataset_filter) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open query plan: " + path);
    }

    std::string line;
    if (!std::getline(file, line)) {
        throw std::runtime_error("query plan is empty: " + path);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    const auto header = QpSplitCsvLine(line);
    std::vector<QueryPlanRow> rows;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (QpTrim(line).empty()) {
            continue;
        }
        const auto fields = QpSplitCsvLine(line);
        const auto raw = QpZipRow(header, fields);
        const std::string dataset = QpGet(raw, "dataset");
        if (!dataset_filter.empty() && dataset != dataset_filter) {
            continue;
        }

        QueryPlanRow row;
        row.dataset = dataset;
        row.baseline_type = QpGet(raw, "baseline_type");
        row.alpha = QpGetInt(raw, "alpha", 1);
        row.repeat = QpGetInt(raw, "repeat", 1);
        row.query_start = QpGet(raw, "actual_query_start", QpGet(raw, "query_start"));
        row.query_end = QpGet(raw, "actual_query_end", QpGet(raw, "query_end"));
        row.lat_min = QpGetDouble(raw, "query_lat_min");
        row.lat_max = QpGetDouble(raw, "query_lat_max");
        row.lon_min = QpGetDouble(raw, "query_lon_min");
        row.lon_max = QpGetDouble(raw, "query_lon_max");
        row.temporal_window_hours = QpGetDouble(raw, "temporal_window_hours");
        row.lat_window_size = QpGetDouble(raw, "lat_window_size");
        row.lon_window_size = QpGetDouble(raw, "lon_window_size");
        row.was_clamped = QpGetBool(raw, "was_clamped");
        row.actual_temporal_scaling_factor =
            QpGetDouble(raw, "actual_temporal_scaling_factor", row.alpha);

        if (row.query_start.empty() || row.query_end.empty()) {
            throw std::runtime_error("query plan row missing query time range");
        }
        rows.push_back(row);
    }
    return rows;
}

inline std::string QueryPlanPathFromEnv(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return "";
    }
    return value;
}

inline void PrintQueryPlanResult(const std::string& scheme,
                                 const QueryPlanRow& row,
                                 double query_gen_ms,
                                 double eval_ms,
                                 double decrypt_ms,
                                 double total_ms,
                                 std::size_t candidate_size,
                                 std::size_t true_result_size,
                                 std::size_t false_positive_count) {
    const double fp_rate = candidate_size == 0
        ? 0.0
        : static_cast<double>(false_positive_count) / static_cast<double>(candidate_size);
    const double selectivity = static_cast<double>(true_result_size);
    std::cout << std::fixed << std::setprecision(8)
              << "[QPLAN_RESULT]"
              << " scheme=" << scheme
              << " dataset=" << row.dataset
              << " baseline_type=" << row.baseline_type
              << " alpha=" << row.alpha
              << " repeat=" << row.repeat
              << " query_start=\"" << row.query_start << "\""
              << " query_end=\"" << row.query_end << "\""
              << " query_lat_min=" << row.lat_min
              << " query_lat_max=" << row.lat_max
              << " query_lon_min=" << row.lon_min
              << " query_lon_max=" << row.lon_max
              << " query_gen_ms=" << query_gen_ms
              << " eval_ms=" << eval_ms
              << " decrypt_ms=" << decrypt_ms
              << " query_latency_ms=" << total_ms
              << " candidate_size=" << candidate_size
              << " true_result_size=" << true_result_size
              << " false_positive_count=" << false_positive_count
              << " false_positive_rate=" << fp_rate
              << " true_result_count_for_selectivity=" << selectivity
              << " was_clamped=" << (row.was_clamped ? 1 : 0)
              << " actual_temporal_scaling_factor=" << row.actual_temporal_scaling_factor
              << "\n";
}
