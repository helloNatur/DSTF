cmake_minimum_required(VERSION 3.10)

# 项目名称和语言
project(JXTTest LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置默认构建类型为 Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# 设置 Debug 模式的编译器标志
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic" CACHE STRING "Flags used by the CXX compiler during Debug builds." FORCE)

# 查找 GTest 包
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# 查找 Threads 包（GTest 需要）
find_package(Threads REQUIRED)

#bf
set(LIBBF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../libbf") 
set(LIBBF_LIBRARY_DIRS "${CMAKE_SOURCE_DIR}/../libbf/build/lib") 
include_directories(${LIBBF_INCLUDE_DIRS})
find_library(BF_LIBRARY NAMES bf  libbf PATHS ${LIBBF_LIBRARY_DIRS})
# if(NOT BF_LIRARAY)
#     message(FATAL_ERROR "libbf not found. Please build it first.")
# endif()


#定义共享的cube_code库
add_library(cube_code STATIC
            serverdb/cube_code.cpp
)
target_include_directories(cube_code
    PUBLIC  # 假设 bf 目录在项目根目录下
    ${GTEST_INCLUDE_DIRS}   # Google Test 头文件
    ${CMAKE_SOURCE_DIR}/serverdb  # serverdb 头文件
    ${CMAKE_SOURCE_DIR}/SegmentTree  # SegmentTree 头文件
    ${CMAKE_SOURCE_DIR}/BPlus  # BPlus 头文件
    ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
)
target_link_libraries(cube_code PUBLIC 
                      pthread
                      ${BF_LIBRARY}  # libbf 库)
)


#添加子目录
add_subdirectory(serverdb)
add_subdirectory(SegmentTree)
add_subdirectory(BPlus)

#启动测试
enable_testing()