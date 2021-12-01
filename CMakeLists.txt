cmake_minimum_required(VERSION 3.17)
project(swoc_ts_api CXX)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)

FetchContent_Declare(
    libSWOC
    GIT_REPOSITORY "https://github.com/solidwallofcode/libswoc"
    GIT_TAG "cmake-fetch"
)

FetchContent_MakeAvailable(libSWOC)

add_library(${PROJECT_NAME} SHARED plugin/src/id_check.cc plugin/src/ts_util.cc)
#target_include_directories(${PROJECT_NAME} PRIVATE plugin/include)
target_link_libraries(${PROJECT_NAME} libswoc)
