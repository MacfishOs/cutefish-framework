cmake_minimum_required(VERSION 3.16)

project(CutefishFramework
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(GNUInstallDirs)
include(CTest)
include(CutefishFrameworkHelpers)

add_subdirectory(clients)
add_subdirectory(files)

cutefish_add_translations(cutefish-framework SOURCES clients common files)
