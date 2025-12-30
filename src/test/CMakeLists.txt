set(LIBBF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/libbf") 
include_directories(${LIBBF_INCLUDE_DIRS})
find_library(BF_LIBRARY NAMES bf PATHS ${CMAKE_SOURCE_DIR}/libbf/build/lib)

# add_executable(JXT_Test
#         JXTp.cpp
# )

# target_link_libraries(JXT_Test PRIVATE utils server_code)
# target_include_directories(JXT_Test PRIVATE ${CMAKE_SOURCE_DIR}/src/include)

#add_executable(Page_Test
#        PageTest.cpp
#)
#
#target_link_libraries(Page_Test PRIVATE utils server_code)
#target_link_libraries(Page_Test pthread)
#target_include_directories(Page_Test  ${CMAKE_SOURCE_DIR}/src/include)
# 添加可执行文件
# add_executable(Page_Test PageTest.cpp)

# # 链接库
# target_link_libraries(Page_Test PRIVATE pthread utils server_code)

# # 添加包含目录
# target_include_directories(Page_Test PRIVATE ${CMAKE_SOURCE_DIR}/src/include)

# 查找 Google Test
find_package(GTest REQUIRED)



# 可执行文件 SegmentTree_Test
# add_executable(SegmentTree_Test
#     SegmentTreeTest.cpp
# )
# target_link_libraries(SegmentTree_Test PRIVATE utils server_code gtest gtest_main pthread)
# target_include_directories(SegmentTree_Test PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${GTEST_INCLUDE_DIRS})



# 可执行文件 IdToken_Test
add_executable(IdTokenInterval_Test
    IdTokenTest_interval.cpp
)
target_link_libraries(IdTokenInterval_Test PRIVATE utils server_code 
                      gtest gtest_main pthread BPlusTree)
target_include_directories(IdTokenInterval_Test PRIVATE ${CMAKE_SOURCE_DIR}/src/include 
                            ${GTEST_INCLUDE_DIRS}
                            ${CMAKE_SOURCE_DIR}/test/SegmentTree  # SegmentTree 头文件
                            ${CMAKE_SOURCE_DIR}/test/serverdb  # serverdb 头文件
                            ${CMAKE_SOURCE_DIR}/test/BPlus  # BPlus 头文件
                            )

# # 可选：确保 Google Test 找到正确的库路径
# if(NOT GTEST_LIBRARIES OR NOT GTEST_MAIN_LIBRARIES)
#     message(FATAL_ERROR "Google Test libraries not found. Please ensure GTest is installed and set GTEST_ROOT if necessary.")
# endif()


# 可执行文件 Analyze10MinIntervals
add_definitions(-DDATA_DIR="${CMAKE_SOURCE_DIR}/data")
add_executable(Analyze10MinIntervals
    Analyze_data.cpp
)
target_link_libraries(Analyze10MinIntervals PRIVATE utils server_code 
                      gtest gtest_main pthread BPlusTree)
target_include_directories(Analyze10MinIntervals PRIVATE ${CMAKE_SOURCE_DIR}/src/include 
                            ${GTEST_INCLUDE_DIRS}
                            ${CMAKE_SOURCE_DIR}/test/SegmentTree  # SegmentTree 头文件
                            ${CMAKE_SOURCE_DIR}/test/serverdb  # serverdb 头文件
                            ${CMAKE_SOURCE_DIR}/test/BPlus  # BPlus 头文件
                            )

# 可选：确保 Google Test 找到正确的库路径
if(NOT GTEST_LIBRARIES OR NOT GTEST_MAIN_LIBRARIES)
    message(FATAL_ERROR "Google Test libraries not found. Please ensure GTest is installed and set GTEST_ROOT if necessary.")
endif()