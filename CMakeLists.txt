cmake_minimum_required(VERSION 3.16)

project(CutefishFramework
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

add_subdirectory(clients)
add_subdirectory(files)
