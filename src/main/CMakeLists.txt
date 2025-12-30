set(LIBBF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/libbf") 
include_directories(${LIBBF_INCLUDE_DIRS})
find_library(BF_LIBRARY NAMES bf PATHS ${CMAKE_SOURCE_DIR}/libbf/build/lib)

add_library(server_code
    Server_JXTp.cpp
    Setup_JXTP.cpp
)

# 打印 CMake 实际命中的配置与 include 目录
find_package(nlohmann_json CONFIG REQUIRED)
message(STATUS "nlohmann_json_DIR = ${nlohmann_json_DIR}")
get_target_property(_incs nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "nlohmann_json include dirs = ${_incs}")

# 主要解决 nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES 包含多个路径的问题
if (_incs STREQUAL "/include" OR _incs STREQUAL "/include;/include")
  set_target_properties(nlohmann_json::nlohmann_json
    PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "/usr/include")
endif()

# include(FetchContent)
# FetchContent_Declare(
#   nlohmann_json
#   GIT_REPOSITORY https://github.com/nlohmann/json.git
#   GIT_TAG v3.11.3
# )
# FetchContent_MakeAvailable(nlohmann_json)
target_link_libraries(server_code PRIVATE utils nlohmann_json::nlohmann_json ${BF_LIBRARY})
target_include_directories(server_code PRIVATE ${CMAKE_SOURCE_DIR}/src/include
                            ${CMAKE_SOURCE_DIR}/test/SegmentTree  # SegmentTree 头文件
                            ${CMAKE_SOURCE_DIR}/test/serverdb  # serverdb 头文件
                            ${CMAKE_SOURCE_DIR}/test/BPlus  # BPlus 头文件
                            )
add_definitions(-DDATA_DIR="${CMAKE_SOURCE_DIR}/data")