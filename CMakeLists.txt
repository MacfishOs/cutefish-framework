cmake_minimum_required(VERSION 3.16)

project(CutefishFramework
    VERSION 1.0.0
    LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

include(GNUInstallDirs)
include(CTest)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Qml
)

# Get the installation directory from Qt 6
find_program(QT_PATHS_EXECUTABLE NAMES qtpaths6 REQUIRED)

execute_process(COMMAND ${QT_PATHS_EXECUTABLE} -query QT_INSTALL_QML
    OUTPUT_VARIABLE INSTALL_QMLDIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(INSTALL_QMLDIR)
    message(STATUS "qml directory:" "${INSTALL_QMLDIR}")
else()
    message(FATAL_ERROR "qml directory cannot be detected.")
endif()

include(CutefishFrameworkQml)
# -------------------------------------------

add_subdirectory(io)
add_subdirectory(application)
add_subdirectory(accounts)
add_subdirectory(bluetooth)
add_subdirectory(media)
add_subdirectory(network)
add_subdirectory(screen)
add_subdirectory(appearance)
add_subdirectory(audio)

# One package for the libraries other Cutefish projects link against; each
# library installs its own targets file next to it.
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
