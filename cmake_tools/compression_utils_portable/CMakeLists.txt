cmake_minimum_required(VERSION 3.8)
project(compression_utils_portable CXX C)

add_library(compression_utils_portable STATIC ${CMAKE_CURRENT_LIST_DIR}/main.cpp)
target_link_libraries(compression_utils_portable PUBLIC zlibstatic)
