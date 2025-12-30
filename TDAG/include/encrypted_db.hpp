#pragma once

#include "emm_interface.hpp"
#include "index_interface.hpp"
#include <memory>
#include <vector>
#include <stdexcept>
#include <chrono>
#include "TimeUtil.h" // <<< 新增：用于性能基准测试


// <<< 新增：用于存储查询各阶段计时的结构体
struct QueryTimings {
    double query_gen_ms = 0.0;
    double eval_ms = 0.0;
    double result_decrypt_ms = 0.0;
};

struct RangeQueryResult {
    QueryTimings timings;
    std::set<int> set_times;
};

/**
 * @class EncryptedSpatialDB
 * @brief The main orchestrator for the encrypted spatial database system.
 *
 * This class combines a spatial indexing scheme (a strategy for mapping spatial
 * data to labels) with an EMM engine (a strategy for executing keyword-based
 * searchable encryption). It provides a high-level API for building the
 * encrypted index and performing range queries.
 */
class EncryptedSpatialDB {
public:
    /**
     * @brief Constructs an EncryptedSpatialDB instance.
     * @param spatial_scheme A unique_ptr to an object implementing the ISpatialIndex interface (e.g., TdagSRC3D).
     * @param emm_engine A unique_ptr to an object implementing the I_EMM interface (e.g., StandardEMM).
     */
    EncryptedSpatialDB(std::shared_ptr<Index_Interface> spatial_scheme,
                       std::shared_ptr<EMM_Interface> emm_engine)
        : spatial_scheme_(spatial_scheme),
          emm_engine_(emm_engine) {
        if (!spatial_scheme_ || !emm_engine_) {
            throw std::invalid_argument("Spatial scheme and EMM engine cannot be null.");
        }
    }

    /**
     * @brief Builds the encrypted index from a map of 3D points to record IDs.
     * @param points The plaintext 3D point database.
     */
    void build(const PointMap3D& points) {
        // Step 1: Use the spatial scheme to map 3D points to keyword labels.
        KeywordMap keyword_map = spatial_scheme_->mapPointsToLabels(points);
        
        // Step 2: Use the EMM engine to build the final encrypted index from the labels.
        emm_engine_->buildEMM(keyword_map);
    }

    /**
     * @brief Performs a range query on the encrypted database.
     * @param query_rect The 3D rectangular region to query.
     * @return A vector of decrypted record IDs found within the queried range.
     * Note: This result is a superset; client-side filtering is needed for exactness.
     */
    std::vector<RecordID> query(const Rect3D& query_rect) {
        // Step 1: Use the spatial scheme to convert the query rectangle into one or more keyword labels.
        std::vector<Label> labels = spatial_scheme_->getQueryLabels(query_rect);

        if (labels.empty()) {
            return {}; // No labels generated for this query.
        }

        // Step 2: Generate search tokens for all resulting labels.
        std::vector<Ciphertext> all_tokens;
        for (const auto& label : labels) {
            auto tokens_for_label = emm_engine_->generateTokens(label);
            all_tokens.insert(all_tokens.end(), tokens_for_label.begin(), tokens_for_label.end());
        }

        // Step 3: The EMM engine executes the search using the tokens.
        EncryptedResult enc_results = emm_engine_->query(all_tokens);
        
        // Step 4: Decrypt the results to get the record IDs.
        return emm_engine_->decryptResults(enc_results);
    }

    /**
     * @brief Retrieves the total storage size of the encrypted index.
     * @return The size in bytes.
     */
    size_t getStorageSize() const {
        if (emm_engine_) {
            return emm_engine_->getStorageSize();
        }
        return 0;
    }

    // <<< 新增：用于性能基准测试的函数
    RangeQueryResult benchmarkRangeQuery(const Rect3D& query_rect) {
        if (!emm_engine_) {
            throw std::runtime_error("EMM engine is not initialized.");
        }

        QueryTimings timings;


        // 1. Query Time: 生成查询令牌
        auto query_search_start = std::chrono::high_resolution_clock::now();
        auto query_labels = spatial_scheme_->getQueryLabels(query_rect);
        auto search_tokens = emm_engine_->generateTokens(query_labels);
        auto query_search_end = std::chrono::high_resolution_clock::now();
        timings.query_gen_ms = std::chrono::duration<double, std::milli>(query_search_end - query_search_start).count();

        // 2. Eval Time: 服务器评估
        auto eval_search_start = std::chrono::high_resolution_clock::now();
        auto encrypted_results = emm_engine_->query(search_tokens);
        auto eval_search_end = std::chrono::high_resolution_clock::now();
        timings.eval_ms = std::chrono::duration<double, std::milli>(eval_search_end - eval_search_start).count();

        // 3. Result Time: 客户端解密
        auto result_search_start = std::chrono::high_resolution_clock::now();
        auto decrypted_ids = emm_engine_->decryptResults(encrypted_results);
        auto result_search_end = std::chrono::high_resolution_clock::now();
        timings.result_decrypt_ms = std::chrono::duration<double, std::milli>(result_search_end - result_search_start).count();

        RangeQueryResult out{
        /*timings=*/timings,
        /*set_time=*/std::set<int>(decrypted_ids.begin(), decrypted_ids.end())
        };
        return out; // 利用返回值优化 / 移动
        
    }

private:
    std::shared_ptr<Index_Interface> spatial_scheme_;
    std::shared_ptr<EMM_Interface> emm_engine_;
};
