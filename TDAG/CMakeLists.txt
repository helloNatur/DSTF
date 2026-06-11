cmake_minimum_required(VERSION 3.10)
project(EMMTest CXX)

#-----------------------------------------------------------------------------
# 1. 项目全局设置
#-----------------------------------------------------------------------------

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置默认构建类型为 Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# 设置 Debug 模式的编译器标志
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic" CACHE STRING "Flags used by the CXX compiler during Debug builds." FORCE)

#-----------------------------------------------------------------------------
# 2. 查找外部依赖包
#-----------------------------------------------------------------------------  

# 查找 Google Test 包
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

find_package(OpenSSL REQUIRED)

set(LIBBF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../libbf") 
include_directories(${LIBBF_INCLUDE_DIRS})
find_library(BF_LIBRARY NAMES bf PATHS ${CMAKE_SOURCE_DIR}/../libbf/build/lib)

#-----------------------------------------------------------------------------
# 3. 定义项目内部的库 (从底层到上层)
#-----------------------------------------------------------------------------

# --- utils 库 ---
add_library(utils STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/Hash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/AESUtil.cpp
    # 如果 utils 目录下还有别的 cpp 一并加上
)
target_include_directories(utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
)
target_link_libraries(utils PUBLIC OpenSSL::Crypto )


# --- cube_code 库 ---
add_library(cube_code ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb/cube_code.cpp)
target_include_directories(cube_code PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
)

# --- SegmentTree 库 ---
add_library(SegmentTree ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree/SegmentTree.cpp)
target_include_directories(SegmentTree PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
    ${CMAKE_CURRENT_SOURCE_DIR}/../libbf  # libbf 头文件
)
target_link_libraries(SegmentTree PUBLIC cube_code ${BF_LIBRARY}) # 依赖 bf

# --- TdagBF 库 ---
add_library(TdagBF ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF/tdag_bf.cpp)
target_include_directories(TdagBF PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
    ${CMAKE_CURRENT_SOURCE_DIR}/../libbf  # libbf 头文件
)
target_link_libraries(TdagBF PUBLIC cube_code ${BF_LIBRARY}) # 依赖 bf

# --- BPlusBPlusTdag 库 ---
add_library(BPlusTdag ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag/bplus_tdag.cpp)
target_include_directories(BPlusTdag PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
)
target_link_libraries(BPlusTdag PUBLIC TdagBF) 

# --- BPlusTree 库 ---
add_library(BPlusTree ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus/bplus_tree.cpp)
target_include_directories(BPlusTree PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
)
target_link_libraries(BPlusTree PUBLIC SegmentTree TdagBF BPlusTdag) 

# --- PeaDRAM day-level hash index adapter ---
add_library(pea_dram_day_index STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pea_dram_day_index.cpp
)
target_include_directories(pea_dram_day_index
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../test/pea_dram/unique/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../test/pea_dram/unique/util
)
target_compile_options(pea_dram_day_index PRIVATE -march=native)
target_link_libraries(pea_dram_day_index PUBLIC pthread)

# --- adaptive time bucket locator shared by DSTF and DSTF+ ---
add_library(adaptive_time_bucket STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adaptive_time_bucket.cpp
)
target_include_directories(adaptive_time_bucket PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


# --- tdag_src_3d  ---
add_library(tdag_src_3d STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/standard_emm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tdag_src_3d.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tdag.cpp
)

target_include_directories(tdag_src_3d
    PRIVATE  # 假设 bf 目录在项目根目录下
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tdag_src_3d PRIVATE utils BPlusTree pthread)


# --- qdag_src_3d  ---
add_library(qdag_src_3d STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/standard_emm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qdag_src_3d.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/quad_tree_3d_src.cpp
)

target_include_directories(qdag_src_3d
    PRIVATE  # 假设 bf 目录在项目根目录下
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(qdag_src_3d PRIVATE utils BPlusTree pthread)

# # 添加测试目标
# add_executable(tdag_src_3d_test tdag_src_3d_test.cpp)
# target_include_directories(tdag_src_3d_test PRIVATE 
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/include
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../BPlus
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../SegmentTree
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../serverdb
#                             ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../../libbf  # libbf 头文件
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../include )
# target_link_libraries(tdag_src_3d_test PRIVATE 
#                       tdag_src_3d
#                       utils
#                       gtest 
#                       gtest_main 
#                       pthread
#                       )

# # 启用测试
# enable_testing()
# add_test(NAME TDAGSRC3DTests COMMAND tdag_src_3d_test)

# # 添加测试目标
# add_executable(standard_emm_test standard_emm_test.cpp)
# target_include_directories(standard_emm_test PRIVATE 
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/include
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../BPlus
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../SegmentTree
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../serverdb
#                             ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../../../libbf  # libbf 头文件
#                             ${CMAKE_CURRENT_SOURCE_DIR}/../include )
# target_link_libraries(standard_emm_test PRIVATE 
#                       tdag_src_3d
#                       utils
#                       gtest 
#                       gtest_main 
#                       pthread
#                       )

# # 启用测试
# enable_testing()
# add_test(NAME StandardEMMTests COMMAND standard_emm_test)

#-----------------------------------------------------------------------------
# 4. 定义可执行文件 (测试程序)
#-----------------------------------------------------------------------------
enable_testing()

# --- 测试目标 0 ---
add_executable(standard_emm_test ${CMAKE_CURRENT_SOURCE_DIR}/test/standard_emm_test.cpp)
target_include_directories(standard_emm_test 
                            PRIVATE  # 假设 bf 目录在项目根目录下
                            ${CMAKE_CURRENT_SOURCE_DIR}/include
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus  # BPlus 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree  # SegmentTree 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb  # serverdb 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include  # src/include 头文件
)
target_link_libraries(standard_emm_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
)
add_test(NAME StandardEMMTests COMMAND standard_emm_test)

# --- 测试目标 1 ---
add_executable(spatiotemporal_db_test ${CMAKE_CURRENT_SOURCE_DIR}/test/spatiotemporal_db_test.cpp)
target_include_directories(spatiotemporal_db_test
                            PRIVATE  # 假设 bf 目录在项目根目录下
                            ${CMAKE_CURRENT_SOURCE_DIR}/include
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus  # BPlus 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree  # SegmentTree 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb  # serverdb 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include  # src/include 头文件
)
target_link_libraries(spatiotemporal_db_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME SpatiotemporalDBTests COMMAND spatiotemporal_db_test)


# --- 测试目标 2 ---
add_executable(b_st_bf_test ${CMAKE_CURRENT_SOURCE_DIR}/test/b_st_bf_test.cpp)
target_include_directories(b_st_bf_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(b_st_bf_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME SpatiotemporalDBBtreeTests COMMAND b_st_bf_test)

# --- hash/ST/BF experiment target ---
add_executable(hash_st_bf_test ${CMAKE_CURRENT_SOURCE_DIR}/test/hash_st_bf_test.cpp)
target_include_directories(hash_st_bf_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${GTEST_INCLUDE_DIRS}
                            ${LIBBF_INCLUDE_DIRS}
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(hash_st_bf_test PRIVATE 
                      tdag_src_3d
                      pea_dram_day_index
                      adaptive_time_bucket
                      gtest 
                      gtest_main 
                      )
add_test(NAME HashSTBFTests COMMAND hash_st_bf_test)

# --- 测试目标 3 ---
add_executable(qdag_src_3d_test ${CMAKE_CURRENT_SOURCE_DIR}/test/qdag_src_3d_test.cpp)
target_include_directories(qdag_src_3d_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(qdag_src_3d_test PRIVATE 
                      qdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME QdagSrc3DTest COMMAND qdag_src_3d_test)

# --- 测试目标 4 ---
add_executable(qdag_src_3d_test2 ${CMAKE_CURRENT_SOURCE_DIR}/test/qdag_src_3d_test2.cpp)
target_include_directories(qdag_src_3d_test2 PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(qdag_src_3d_test2 PRIVATE 
                      qdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME QdagSrc3DTest2 COMMAND qdag_src_3d_test2)


# --- 测试目标 5 ---
add_executable(tdag_1d_test ${CMAKE_CURRENT_SOURCE_DIR}/test/tdag_1d_test.cpp)
target_include_directories(tdag_1d_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(tdag_1d_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME TDAGTest COMMAND tdag_1d_test)


# --- 测试目标 6 ---
add_executable(tdag_bf_test ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF/tdag_bf_test.cpp)
target_include_directories(tdag_bf_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(tdag_bf_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME TdagBFTest COMMAND tdag_bf_test)


# --- 测试目标 7 ---
add_executable(bplus_tdag_test ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag/bplus_tdag_test.cpp)
target_include_directories(bplus_tdag_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(bplus_tdag_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME BPlusTdagTest COMMAND bplus_tdag_test)


# --- 测试目标 8 ---
add_executable(bplus_tree_test ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus/bplus_tree_test.cpp)
target_include_directories(bplus_tree_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(bplus_tree_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME BPlusTreeTest COMMAND bplus_tree_test)


# --- 测试目标 9 ---
add_executable(b_tdag_bf_test ${CMAKE_CURRENT_SOURCE_DIR}/test/b_tdag_bf_test.cpp)
target_include_directories(b_tdag_bf_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(b_tdag_bf_test PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME BPTdagBFTest COMMAND b_tdag_bf_test)

# --- hash/TDAG/BF experiment target ---
add_executable(hash_tdag_bf_test ${CMAKE_CURRENT_SOURCE_DIR}/test/hash_tdag_bf_test.cpp)
target_include_directories(hash_tdag_bf_test PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
                            ${GTEST_INCLUDE_DIRS}
                            ${LIBBF_INCLUDE_DIRS}
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(hash_tdag_bf_test PRIVATE 
                      tdag_src_3d
                      pea_dram_day_index
                      adaptive_time_bucket
                      gtest 
                      gtest_main 
                      )
add_test(NAME HashTdagBFTest COMMAND hash_tdag_bf_test)


# --- 测试目标 3 ---
add_executable(qdag_src_3d_test_v_datasets ${CMAKE_CURRENT_SOURCE_DIR}/test/qdag_src_3d_test_v_datasets.cpp)
target_include_directories(qdag_src_3d_test_v_datasets PRIVATE 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_link_libraries(qdag_src_3d_test_v_datasets PRIVATE 
                      qdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME QdagSrc3DTestVDatasets COMMAND qdag_src_3d_test_v_datasets)


# --- 测试目标 1 ---
add_executable(spatiotemporal_db_test1 ${CMAKE_CURRENT_SOURCE_DIR}/test/spatiotemporal_db_test_v_datasets.cpp)
target_include_directories(spatiotemporal_db_test1
                            PRIVATE  # 假设 bf 目录在项目根目录下
                            ${CMAKE_CURRENT_SOURCE_DIR}/include
                            ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus  # BPlus 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree  # SegmentTree 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb  # serverdb 头文件
                            ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include  # src/include 头文件
)
target_link_libraries(spatiotemporal_db_test1 PRIVATE 
                      tdag_src_3d
                      gtest 
                      gtest_main 
                      )
add_test(NAME SpatiotemporalDBTests1 COMMAND spatiotemporal_db_test1)

# --- Logarithmic-SRC-i1 time-axis variant for spatiotemporal_db_test ---
add_executable(i1_spatiotemporal_db_test ${CMAKE_CURRENT_SOURCE_DIR}/test/i1_spatiotemporal_db_test.cpp)
target_include_directories(i1_spatiotemporal_db_test PRIVATE
                            ${CMAKE_CURRENT_SOURCE_DIR}/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/test
                            ${GTEST_INCLUDE_DIRS}
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${LIBBF_INCLUDE_DIRS}
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include )
target_link_libraries(i1_spatiotemporal_db_test PRIVATE
                      tdag_src_3d
                      gtest
                      gtest_main
                      )
add_test(NAME I1SpatiotemporalDBTests COMMAND i1_spatiotemporal_db_test)

# --- B+ day localization + Logarithmic-SRC-i1 interval index ---
add_executable(i1_b_tdag_bf_test ${CMAKE_CURRENT_SOURCE_DIR}/test/i1_b_tdag_bf_test.cpp)
target_include_directories(i1_b_tdag_bf_test PRIVATE
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
                            ${GTEST_INCLUDE_DIRS}
                            ${LIBBF_INCLUDE_DIRS}
                            ${CMAKE_CURRENT_SOURCE_DIR}/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/test )
target_link_libraries(i1_b_tdag_bf_test PRIVATE
                      tdag_src_3d
                      BPlusTdag
                      TdagBF
                      gtest
                      gtest_main
                      )
add_test(NAME I1BPTdagBFTest COMMAND i1_b_tdag_bf_test)

# --- PeaHash day localization + Logarithmic-SRC-i1 interval index ---
add_executable(i1_hash_tdag_bf_test ${CMAKE_CURRENT_SOURCE_DIR}/test/i1_hash_tdag_bf_test.cpp)
target_include_directories(i1_hash_tdag_bf_test PRIVATE
                            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlus
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/BPlusTdag
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/SegmentTree
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/serverdb
                            ${CMAKE_CURRENT_SOURCE_DIR}/../test/TdagBF
                            ${GTEST_INCLUDE_DIRS}
                            ${LIBBF_INCLUDE_DIRS}
                            ${CMAKE_CURRENT_SOURCE_DIR}/include
                            ${CMAKE_CURRENT_SOURCE_DIR}/test )
target_link_libraries(i1_hash_tdag_bf_test PRIVATE
                      tdag_src_3d
                      pea_dram_day_index
                      gtest
                      gtest_main
                      )
add_test(NAME I1HashTdagBFTest COMMAND i1_hash_tdag_bf_test)
