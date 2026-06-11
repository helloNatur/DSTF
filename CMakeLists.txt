cmake_minimum_required(VERSION 3.10)
project(JXT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # 查找 Google Test
# find_package(GTest REQUIRED)
# include_directories(${GTEST_INCLUDE_DIRS})

#设置默认构建类型为debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type pf build." FORCE)
endif()

#设置debug模式下的编译器标志
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

# 设置头文件目录
include_directories(${CMAKE_SOURCE_DIR}/src/include)
find_package(OpenSSL REQUIRED)

# 添加子目录
add_subdirectory(src/utils)
add_subdirectory(src/main)
add_subdirectory(src/test)
add_subdirectory(test)


