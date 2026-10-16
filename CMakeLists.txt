cmake_minimum_required(VERSION 3.16)

project(CutefishFramework
    VERSION 1.0.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

add_subdirectory(protocols)
add_subdirectory(clients)
add_subdirectory(files)

# The CutefishFramework package holds the protocol bindings; clients/ and
# files/ install packages of their own.
set(CUTEFISH_FRAMEWORK_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework)
configure_package_config_file(cmake/CutefishFrameworkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CutefishFrameworkConfig.cmake
    INSTALL_DESTINATION ${CUTEFISH_FRAMEWORK_CMAKEDIR}
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/CutefishFrameworkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CutefishFrameworkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CutefishFrameworkConfigVersion.cmake
    DESTINATION ${CUTEFISH_FRAMEWORK_CMAKEDIR}
)
