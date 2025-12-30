if (POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif ()

project(pehash)
cmake_minimum_required(VERSION 3.11)
include(FetchContent)
include(ExternalProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-long-long -fPIC -march=native")
set(CMAKE_ENABLE_COMPILE_COMMANDS "ON")

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
  message("-- Release mode, all optimizations enabled")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG -Ofast")
endif ()

set(libs_to_link
        pthread gflags      
        )

# libbf configuration
set(LIBBF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../../../libbf") 
set(LIBBF_LIB_DIR "${CMAKE_SOURCE_DIR}/../../../libbf/build/lib")
# Set global include directory for libbf (needed by SegmentTree)
include_directories(${LIBBF_INCLUDE_DIRS})
# Try to find libbf library
if(EXISTS "${LIBBF_LIB_DIR}/libbf.so")
    set(BF_LIBRARY "${LIBBF_LIB_DIR}/libbf.so")
    message(STATUS "Found libbf: ${BF_LIBRARY}")
elseif(EXISTS "${LIBBF_LIB_DIR}/libbf.a")
    set(BF_LIBRARY "${LIBBF_LIB_DIR}/libbf.a")
    message(STATUS "Found libbf: ${BF_LIBRARY}")
else()
    message(WARNING "libbf library not found at ${LIBBF_LIB_DIR}, but continuing anyway")
    set(BF_LIBRARY "")
endif()



add_executable(test_pmem test/test_pmem.cpp)
target_include_directories(test_pmem PRIVATE 
    util
    ${CMAKE_SOURCE_DIR}/../../SegmentTree  # SegmentTree 头文件 (test/SegmentTree)
    ${CMAKE_SOURCE_DIR}/../../../src/include  # TimeUtil 头文件 (src/include)
    ${LIBBF_INCLUDE_DIRS}  # libbf 头文件
)
# Add cube_code library
add_library(cube_code STATIC
    ${CMAKE_SOURCE_DIR}/../../serverdb/cube_code.cpp
)
target_include_directories(cube_code PUBLIC
    ${CMAKE_SOURCE_DIR}/../../serverdb
)

# Add SegmentTree library
add_subdirectory(${CMAKE_SOURCE_DIR}/../../SegmentTree SegmentTree_build)

if(BF_LIBRARY)
    target_link_libraries(test_pmem PRIVATE ${libs_to_link} ${BF_LIBRARY} SegmentTree)
else()
    target_link_libraries(test_pmem PRIVATE ${libs_to_link} SegmentTree)
endif()

# add_definitions(-DPEA_ONLY)
# add_definitions(-DAVX512F)
