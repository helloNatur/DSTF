# cmake_policy(SET CMP0002 NEW)  # 防止 target 重复命名报错
# cmake_policy(SET CMP0079 NEW)  # 允许跨目录 target_link_libraries

cmake_minimum_required(VERSION 3.10)
project(ServerDBTest CXX)

# 设置 C++ 标准
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # 设置默认构建类型为 Debug
# if(NOT CMAKE_BUILD_TYPE)
#     set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
# endif()

# # 设置 Debug 模式的编译器标志
# set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic" CACHE STRING "Flags used by the CXX compiler during Debug builds." FORCE)

# # 查找 Google Test 包
# find_package(GTest REQUIRED)
# include_directories(${GTEST_INCLUDE_DIRS})


# #添加cube——code库
# add_library(cube_code STATIC
#     cube_code.cpp
# )

# target_include_directories(cube_code
#     PRIVATE  # 假设 bf 目录在项目根目录下
#     ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
# )

# target_link_libraries(cube_code PRIVATE pthread)

# 添加测试目标
add_executable(serverdb_cube_code_test cube_code_test.cpp)
target_include_directories(serverdb_cube_code_test PRIVATE 
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_SOURCE_DIR}/../SegmentTree
                           ${CMAKE_SOURCE_DIR}/../BPlus)
target_link_libraries(serverdb_cube_code_test PRIVATE 
                      cube_code
                      gtest 
                      gtest_main 
                      pthread)

# # 启用测试
# enable_testing()
add_test(NAME CubeCodeTests COMMAND serverdb_cube_code_test)