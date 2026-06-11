#pragma once

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>

namespace idtoken_metrics {

inline std::size_t GetEnvSizeT(const char* name, std::size_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        return static_cast<std::size_t>(std::stoull(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

inline int GetEnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        return std::max(1, std::stoi(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

inline std::string GetEnvString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return value;
}

inline std::set<std::string> LoadExpectedIds(const std::string& path) {
    std::set<std::string> expected;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[False Positive] expected_path_open_failed=" << path << "\n";
        return expected;
    }

    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::stringstream ss(line);
        std::string id1;
        std::string id2;
        if (std::getline(ss, id1, ',') && std::getline(ss, id2, ',')) {
            if (!id1.empty()) expected.insert(id1);
            if (!id2.empty()) expected.insert(id2);
        }
    }
    return expected;
}

inline void PrintFinalResultStats(const std::set<std::string>& decrypted_range,
                                  const std::set<std::string>& decrypted_stag1,
                                  const std::string& default_expected_path) {
    std::set<std::string> final_result;
    std::set_intersection(decrypted_range.begin(), decrypted_range.end(),
                          decrypted_stag1.begin(), decrypted_stag1.end(),
                          std::inserter(final_result, final_result.begin()));

    std::cout << "[Result] range_decrypted=" << decrypted_range.size()
              << ", stag_decrypted=" << decrypted_stag1.size()
              << ", final_result=" << final_result.size() << "\n";

    const std::string expected_path =
        GetEnvString("JXT2_EXPECTED_PATH", default_expected_path);
    auto expected = LoadExpectedIds(expected_path);
    if (expected.empty()) {
        std::cout << "[False Positive] expected=0, true_positive=0, false_positive="
                  << final_result.size() << ", false_negative=0, fp_rate="
                  << (final_result.empty() ? 0.0 : 1.0) << "\n";
        return;
    }

    std::set<std::string> true_positive;
    std::set_intersection(final_result.begin(), final_result.end(),
                          expected.begin(), expected.end(),
                          std::inserter(true_positive, true_positive.begin()));
    const std::size_t false_positive = final_result.size() - true_positive.size();
    const std::size_t false_negative = expected.size() - true_positive.size();
    const double fp_rate = final_result.empty()
        ? 0.0
        : static_cast<double>(false_positive) / static_cast<double>(final_result.size());

    std::cout << std::fixed << std::setprecision(6)
              << "[False Positive] expected_path=" << expected_path
              << ", expected=" << expected.size()
              << ", true_positive=" << true_positive.size()
              << ", false_positive=" << false_positive
              << ", false_negative=" << false_negative
              << ", fp_rate=" << fp_rate << "\n";
}

}  // namespace idtoken_metrics
