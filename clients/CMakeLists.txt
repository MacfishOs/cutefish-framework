# Qt-free clients of system services, one library per directory, linking only
# the toolkit's base library. They form the CutefishClients package; a
# consumer asks for them by directory name:
#   find_package(CutefishClients REQUIRED COMPONENTS appearance)
#   target_link_libraries(app PRIVATE Cutefish::AppearanceClient)

find_package(CutefishToolkit REQUIRED)

set(CUTEFISH_FRAMEWORK_CLIENTS "all" CACHE STRING
    "The clients/ directories to build: all, or a list of their names")

set(CUTEFISH_CLIENTS_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishClients)

# cutefish_add_client(<name> TARGET <Target> SOURCES ... HEADERS ... [LIBRARIES ...])
# builds libcutefish-<name>-client as Cutefish::<Target>; headers install as
# <cutefish-clients/<name>/...>, and in the build tree as "<name>/...".
function(cutefish_add_client name)
    cmake_parse_arguments(arg "" "TARGET" "SOURCES;HEADERS;LIBRARIES" ${ARGN})
    set(library cutefish-${name}-client)
    add_library(${library} SHARED ${arg_SOURCES} ${arg_HEADERS})
    add_library(Cutefish::${arg_TARGET} ALIAS ${library})
    set_target_properties(${library} PROPERTIES
        EXPORT_NAME ${arg_TARGET}
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        AUTOMOC OFF
        AUTORCC OFF
    )
    target_include_directories(${library} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cutefish-clients>
    )
    target_link_libraries(${library} PUBLIC Cutefish::ToolkitBase ${arg_LIBRARIES})
    install(TARGETS ${library} EXPORT CutefishClient${name}Targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES ${arg_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cutefish-clients/${name})
    install(EXPORT CutefishClient${name}Targets
        NAMESPACE Cutefish::
        DESTINATION ${CUTEFISH_CLIENTS_CMAKEDIR}
    )
endfunction()

file(GLOB client_directories LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(directory ${client_directories})
    get_filename_component(name ${directory} NAME)
    if(NOT EXISTS ${directory}/CMakeLists.txt)
        continue()
    endif()
    if(CUTEFISH_FRAMEWORK_CLIENTS STREQUAL "all" OR name IN_LIST CUTEFISH_FRAMEWORK_CLIENTS)
        add_subdirectory(${name})
    endif()
endforeach()

configure_package_config_file(CutefishClientsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CutefishClientsConfig.cmake
    INSTALL_DESTINATION ${CUTEFISH_CLIENTS_CMAKEDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CutefishClientsConfig.cmake DESTINATION ${CUTEFISH_CLIENTS_CMAKEDIR})
