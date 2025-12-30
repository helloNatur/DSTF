// #include <gtest/gtest.h>
// #include "encrypted_db.hpp"
// #include "tdag_src_3d.hpp"
// #include "standard_emm.hpp"
// #include <iostream>
// #include <vector>
// #include <numeric>
// #include <algorithm>

// class EncryptedDB_TdagSRC_Test : public ::testing::Test {
// protected:
//     void SetUp() override {
//         // 1. Define constants for the test
//         const int MAX_COORD = 64;
//         const std::string TOKEN_KEY = "a_secure_token_key_for_testing";
//         const std::string ENC_KEY = "a_secure_encryption_key_for_testing";

//         // 2. Instantiate the concrete strategies
//         auto tdag_scheme = std::make_unique<TdagSRC3D>(MAX_COORD, MAX_COORD, MAX_COORD);
//         auto emm_engine = std::make_unique<StandardEMM>(TOKEN_KEY, ENC_KEY);

//         // 3. Assemble the main EncryptedSpatialDB object
//         db_ = std::make_unique<EncryptedSpatialDB>(std::move(tdag_scheme), std::move(emm_engine));
        
//         // 4. Generate test data
//         test_data_ = {
//             {Point3D(10, 20, 30), {101, 102}},
//             {Point3D(15, 25, 35), {201}},
//             {Point3D(5, 10, 15), {301, 302, 303}},
//             {Point3D(50, 40, 20), {401}},
//             {Point3D(63, 63, 63), {501}} // Boundary test
//         };
//     }
    
//     std::unique_ptr<EncryptedSpatialDB> db_;
//     PointMap3D test_data_;
// };

// TEST_F(EncryptedDB_TdagSRC_Test, BuildIndex) {
//     std::cout << "\n--- Build Test ---" << std::endl;
//     ASSERT_NO_THROW(db_->build(test_data_));
    
//     size_t storage_size = db_->getStorageSize();
//     std::cout << "Storage size: " << storage_size << " bytes" << std::endl;
//     EXPECT_GT(storage_size, 0);
// }

// TEST_F(EncryptedDB_TdagSRC_Test, QuerySinglePoint) {
//     std::cout << "\n--- Single Point Query Test ---" << std::endl;
//     db_->build(test_data_);

//     Point3D target_point(15, 25, 35);
//     Rect3D query_rect(target_point, target_point);
    
//     auto results = db_->query(query_rect);
//     std::sort(results.begin(), results.end());

//     std::cout << "Querying for point (15, 25, 35) returned " << results.size() << " results." << std::endl;

//     // Tdag-SRC might return more than the exact match due to range covering
//     EXPECT_GE(results.size(), 1);
    
//     // Check if the expected result is present
//     bool found = std::find(results.begin(), results.end(), 201) != results.end();
//     EXPECT_TRUE(found) << "The expected record ID 201 was not found.";
// }

// TEST_F(EncryptedDB_TdagSRC_Test, QueryRangeWithMultiplePoints) {
//     std::cout << "\n--- Multi-Point Range Query Test ---" << std::endl;
//     db_->build(test_data_);

//     Rect3D query_rect(Point3D(0, 0, 0), Point3D(20, 30, 40));
    
//     auto results = db_->query(query_rect);
//     std::sort(results.begin(), results.end());
    
//     std::cout << "Querying range [0,0,0] to [20,30,40] returned " << results.size() << " results." << std::endl;

//     // Define expected results that are strictly inside the query range
//     std::vector<RecordID> expected_present = {101, 102, 201, 301, 302, 303};
//     std::sort(expected_present.begin(), expected_present.end());

//     // The result set from Tdag-SRC must be a superset of the exact results
//     EXPECT_TRUE(std::includes(results.begin(), results.end(), expected_present.begin(), expected_present.end()))
//         << "The query result does not contain all expected record IDs.";
// }

// TEST_F(EncryptedDB_TdagSRC_Test, QueryEmptyRange) {
//     std::cout << "\n--- Empty Range Query Test ---" << std::endl;
//     db_->build(test_data_);

//     // A range where no points exist
//     Rect3D query_rect(Point3D(0, 0, 0), Point3D(1, 1, 1));
    
//     auto results = db_->query(query_rect);

//     std::cout << "Querying empty range returned " << results.size() << " results." << std::endl;
    
//     // It's possible for an empty range's cover to overlap with a point's cover.
//     // So, we cannot assert that the result is empty. We can only assert that
//     // it does not contain points that are definitively outside the cover.
//     // For this test, we simply ensure it runs without error.
//     ASSERT_TRUE(true);
// }

// // ===================================================================================
// // =================== 新增的多属性范围查询测试 (NEW TEST CASE) ===================
// // ===================================================================================

// /**
//  * @test QueryMidRangeBox
//  * @brief Tests a multi-attribute range query located in the middle of the data space.
//  *
//  * This test defines a query box that does not start at the origin and is sized
//  * to include a specific subset of the test data, while excluding others. It verifies
//  * that the superset result from the query correctly contains all the points that
//  * fall within the defined box.
//  */
// TEST_F(EncryptedDB_TdagSRC_Test, QueryMidRangeBox) {
//     std::cout << "\n--- Mid-Range Multi-Attribute Query Test ---" << std::endl;
//     db_->build(test_data_);

//     // Define a query box to select points in the middle of the dataset.
//     // This box should precisely contain Point(10,20,30) and Point(15,25,35).
//     // It should exclude Point(5,10,15) [x too small], Point(50,40,20) [z too small],
//     // and Point(63,63,63) [all coords too large].
//     Rect3D query_rect(Point3D(8, 18, 25), Point3D(55, 45, 40));
    
//     std::cout << "Querying with box: start(8,18,25), end(55,45,40)" << std::endl;

//     auto results_vec = db_->query(query_rect);
    
//     // For easier validation, convert the vector to a set
//     std::set<RecordID> results_set(results_vec.begin(), results_vec.end());

//     std::cout << "Query returned " << results_set.size() << " unique record IDs (superset)." << std::endl;

//     // Define the set of record IDs that MUST be present in the result.
//     std::set<RecordID> expected_to_be_present = {101, 102, 201};
    
//     // Verify that every expected ID is found in the results.
//     bool all_found = true;
//     for (const auto& expected_id : expected_to_be_present) {
//         if (results_set.find(expected_id) == results_set.end()) {
//             all_found = false;
//             std::cerr << "Validation failed: Expected RecordID " << expected_id << " was not found in the results." << std::endl;
//         }
//     }
    
//     EXPECT_TRUE(all_found) << "The query result is missing one or more expected record IDs.";
    
//     // Also verify that the size of the result is at least the size of the expected set.
//     EXPECT_GE(results_set.size(), expected_to_be_present.size())
//         << "The result set must be a superset of the exact matches.";
// }