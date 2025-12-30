# 查找 GTest 包
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# 查找 Threads 包（GTest 需要）
find_package(Threads REQUIRED)

enable_testing()
add_executable(BasicBloomFilter-Test BasicBloomFilterTest.cpp)

target_link_libraries(BasicBloomFilter-Test 
                        libbf_shared 
                        ${CMAKE_THREAD_LIBS_INIT}
                        gtest
                        gtest_main
                        pthread)

add_test(NAME BasicBloomFilter-Test
        COMMAND ${CMAKE_BINARY_DIR}/bin/BasicBloomFilter-Test
)

