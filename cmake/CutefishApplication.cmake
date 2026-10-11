include(GNUInstallDirs)

# cutefish-booster loads applications from here; the runtime finds a module by
# the file name of the executable it was asked to start.
set(CUTEFISH_APPLICATION_MODULE_DIR "${CMAKE_INSTALL_LIBDIR}/cutefish/applications")

# Builds the application's code as the module <name>.so, which defines
# cutefish_application_main(), and the executable <name> that runs it without
# the booster. Link the application's libraries to <name>-module.
function(cutefish_add_application name)
    cmake_parse_arguments(arg "" "" "SOURCES" ${ARGN})

    add_library(${name}-module SHARED ${arg_SOURCES})
    set_target_properties(${name}-module PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${name}
    )
    target_link_libraries(${name}-module PUBLIC Cutefish::Application)

    set(main "${CMAKE_CURRENT_BINARY_DIR}/${name}-main.cpp")
    file(CONFIGURE OUTPUT "${main}" CONTENT [[
#include <cutefishapplication.h>

int main(int argc, char *argv[])
{
    return Cutefish::Application::run(argc, argv, cutefish_application_main);
}
]])

    add_executable(${name} "${main}")
    target_link_libraries(${name} PRIVATE ${name}-module)
    set_target_properties(${name} PROPERTIES
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CUTEFISH_APPLICATION_MODULE_DIR}"
    )

    install(TARGETS ${name}-module LIBRARY DESTINATION ${CUTEFISH_APPLICATION_MODULE_DIR})
    install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()
