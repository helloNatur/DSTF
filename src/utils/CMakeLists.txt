set(LIBBF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/libbf") 
include_directories(${LIBBF_INCLUDE_DIRS})
find_library(BF_LIBRARY NAMES bf PATHS ${CMAKE_SOURCE_DIR}/libbf/build/lib)



# 构建 utils 库
add_library(utils
    AESUtil.cpp
    Bloom.cpp
    Hash.cpp
    tool.cpp
    Xor8.cpp
    PageManagement.cpp
    # SegmentTree.cpp
)
target_include_directories(utils 
                            PUBLIC 
                            ${CMAKE_SOURCE_DIR}/src/include)


target_link_libraries(utils PUBLIC ${BF_LIBRARY} OpenSSL::Crypto )

