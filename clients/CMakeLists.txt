# Clients of system services, one library per directory, linking only
# the toolkit's base library. They form the CutefishClients package; a
# consumer asks for them by directory name:
#   find_package(CutefishClients REQUIRED COMPONENTS appearance)
#   target_link_libraries(app PRIVATE Cutefish::AppearanceClient)

find_package(CutefishToolkit REQUIRED)

set(CUTEFISH_FRAMEWORK_CLIENTS "all" CACHE STRING
    "The clients/ directories to build: all, or a list of their names")

# cutefish_add_client(<name> TARGET <Target> SOURCES ... HEADERS ... [LIBRARIES ...])
# builds libcutefish-<name>-client as Cutefish::<Target>, its headers included
# as <cutefish/<name>/...>.
function(cutefish_add_client name)
    cmake_parse_arguments(arg "" "TARGET" "SOURCES;HEADERS;LIBRARIES" ${ARGN})
    cutefish_framework_add_library(cutefish-${name}-client
        TARGET ${arg_TARGET}
        MODULE ${name}
        EXPORT CutefishClient${name}Targets
        SOURCES ${arg_SOURCES}
        HEADERS ${arg_HEADERS}
        PUBLIC_LIBRARIES Cutefish::ToolkitBase ${arg_LIBRARIES}
    )
    cutefish_framework_export(CutefishClients CutefishClient${name}Targets)
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

cutefish_framework_add_package(CutefishClients)
