cmake_minimum_required(VERSION 3.21)

project(boost_gateway
    VERSION 1.0.0
    DESCRIPTION "A high-performance C++ game server framework"
    LANGUAGES CXX
)

include(CTest)

option(ENABLE_WARNINGS "Enable compiler warnings" ON)
option(ENABLE_TESTING "Build tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

add_subdirectory(src)
add_subdirectory(examples)

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
