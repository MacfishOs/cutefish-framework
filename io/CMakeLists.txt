cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CutefishIO VERSION 1.0.0 LANGUAGES CXX)
    include(CTest)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Cutefish IO requires Linux")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)

set(IO_PUBLIC_HEADERS
    volumemanager.h error.h filepath.h filesystemurl.h fileitem.h fileenumerator.h
    operationcontext.h localfilesystem.h filesystem.h
    filewatcher.h directorylister.h directorymodel.h directory.h task.h taskprogress.h
    thumbnailcache.h thumbnailprovider.h backgroundrelease.h)

add_library(cutefish-framework-io STATIC ${IO_PUBLIC_HEADERS}
    volumemanager.cpp error.cpp filepath.cpp filesystemurl.cpp fileitem.cpp fileenumerator.cpp
    operationcontext.cpp localfilesystem.cpp trash.cpp filewatcher.cpp
    directorylister.cpp directorymodel.cpp task.cpp thumbnailcache.cpp thumbnailprovider.cpp
    thumbnailers.cpp thumbnailers_p.h mimesniffer.cpp mimesniffer_p.h async.h posix_p.h
    trashdirectory.cpp trashdirectory_p.h)
add_library(Cutefish::IO ALIAS cutefish-framework-io)

set_target_properties(cutefish-framework-io PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_compile_features(cutefish-framework-io PUBLIC cxx_std_20)
target_compile_definitions(cutefish-framework-io PRIVATE _FILE_OFFSET_BITS=64)
target_include_directories(cutefish-framework-io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/Cutefish/IO>)
target_link_libraries(cutefish-framework-io PUBLIC Qt6::Core Qt6::DBus Qt6::Gui Qt6::Quick)

if(BUILD_TESTING OR FILEMANAGER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test Gui)

    add_executable(tst_cutefish_io tests/tst_io.cpp)
    set_target_properties(tst_cutefish_io PROPERTIES AUTOMOC ON)
    target_link_libraries(tst_cutefish_io PRIVATE Cutefish::IO Qt6::Test Qt6::Gui)
    add_test(NAME cutefish-io COMMAND tst_cutefish_io)

    add_executable(tst_mimesniffer tests/tst_mimesniffer.cpp)
    set_target_properties(tst_mimesniffer PROPERTIES AUTOMOC ON)
    target_link_libraries(tst_mimesniffer PRIVATE Cutefish::IO Qt6::Test)
    add_test(NAME mimesniffer COMMAND tst_mimesniffer)

    add_executable(tst_thumbnailers tests/tst_thumbnailers.cpp)
    set_target_properties(tst_thumbnailers PROPERTIES AUTOMOC ON)
    target_link_libraries(tst_thumbnailers PRIVATE Cutefish::IO Qt6::Test Qt6::Gui)
    add_test(NAME thumbnailers COMMAND tst_thumbnailers)

    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_executable(tst_volumemanager tests/tst_volumemanager.cpp)
    set_target_properties(tst_volumemanager PROPERTIES AUTOMOC ON)
    target_link_libraries(tst_volumemanager PRIVATE Cutefish::IO Qt6::Test)
    add_test(NAME volumemanager COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_volumemanager>)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set_target_properties(cutefish-framework-io PROPERTIES EXPORT_NAME IO)
install(TARGETS cutefish-framework-io EXPORT CutefishIOTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${IO_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/IO)
install(EXPORT CutefishIOTargets NAMESPACE Cutefish:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishIO)

configure_package_config_file(CutefishIOConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/CutefishIOConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishIO)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/CutefishIOConfigVersion.cmake
    VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CutefishIOConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/CutefishIOConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishIO)
