cmake_minimum_required(VERSION 3.10)

# 项目名称和语言
project(SegmentTreeTest LANGUAGES CXX)

# 设置 C++ 标准
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS OFF)

# # 设置默认构建类型为 Debug
# if(NOT CMAKE_BUILD_TYPE)
#     set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
# endif()

# # 设置 Debug 模式的编译器标志
# set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic" CACHE STRING "Flags used by the CXX compiler during Debug builds." FORCE)

# 查找 GTest 包
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# 查找 Threads 包（GTest 需要）
find_package(Threads REQUIRED)

# # 假设 bloom_filter 库是通过外部库引入的
# # 如果 bloom_filter 是通过系统安装的，可以用 find_package
# # 这里假设 bloom_filter 的头文件和库文件在系统中可用
# # 如果需要手动指定路径，可以添加以下内容：
# include_directories(/path/to/bloom_filter/include)
# link_directories(/path/to/bloom_filter/lib)

set(LIBBF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/libbf") 
include_directories(${LIBBF_INCLUDE_DIRS})
find_library(BF_LIBRARY NAMES bf PATHS ${CMAKE_SOURCE_DIR}/libbf/build/lib)
# if(NOT BF_LIRARAY)
#     message(FATAL_ERROR "libbf not found. Please build it first.")
# endif()
# add_subdirectory(${CMAKE_SOURCE_DIR}/serverdb serverd_build)

# 添加 SegmentTree 库
add_library(SegmentTree STATIC
    SegmentTree.cpp
)


target_include_directories(SegmentTree
    PUBLIC  # 假设 bf 目录在项目根目录下
    ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
    ${CMAKE_SOURCE_DIR}/test/serverdb  # serverdb 头文件
)

# 链接 bloom_filter 库（假设系统已安装）
# 如果需要手动链接，替换为实际库名和路径
target_link_libraries(SegmentTree PUBLIC cube_code ${BF_LIBRARY})

# 添加测试可执行文件
add_executable(SegmentTreeTest
    SegmentTreeTest.cpp
)

target_include_directories(SegmentTreeTest
    PUBLIC  # 假设 bf 目录在项目根目录下
    ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
    ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
    ${CMAKE_SOURCE_DIR}/test/serverdb  # serverdb 头文件
)

# 链接 SegmentTree 库和 GTest
target_link_libraries(SegmentTreeTest PUBLIC
    SegmentTree
    cube_code
    ${BF_LIBRARY}
    gtest
    gtest_main
    Threads::Threads
)

# 启用测试
# enable_testing()
add_test(NAME SegmentTreeTest COMMAND SegmentTreeTest)