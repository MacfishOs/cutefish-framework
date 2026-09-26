find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)

# The compositor's display D-Bus client, for services and the shell as well as
# the QML module below.
set(DISPLAY_CONFIG_XML ${CMAKE_CURRENT_SOURCE_DIR}/com.cutefish.DisplayConfig1.xml)
set_source_files_properties(${DISPLAY_CONFIG_XML} PROPERTIES INCLUDE displaytypes.h)
qt_add_dbus_interface(DISPLAY_CONFIG_DBUS_SOURCES ${DISPLAY_CONFIG_XML} display_config_interface)

add_library(cutefish-framework-display SHARED
    displayclient.cpp
    displayclient.h
    displaytypes.cpp
    displaytypes.h
    ${DISPLAY_CONFIG_DBUS_SOURCES}
)
add_library(Cutefish::Display ALIAS cutefish-framework-display)

target_include_directories(cutefish-framework-display PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Display>
)
target_link_libraries(cutefish-framework-display PUBLIC
    Qt6::Core
    Qt6::DBus
)

set_target_properties(cutefish-framework-display PROPERTIES EXPORT_NAME Display)
install(TARGETS cutefish-framework-display EXPORT CutefishFrameworkDisplayTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    displayclient.h
    displaytypes.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Display
)
install(EXPORT CutefishFrameworkDisplayTargets
    NAMESPACE Cutefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
)

set(CUTEFISH_SCREEN_SOURCES
    output.cpp
    output.h
    outputmodel.cpp
    outputmodel.h
    screen.cpp
    screen.h
)

add_library(cutefish-framework-screen SHARED ${CUTEFISH_SCREEN_SOURCES})
add_library(Cutefish::Screen ALIAS cutefish-framework-screen)

qt_add_qml_module(cutefish-framework-screen
    URI Cutefish.Screen
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-screen
    CLASS_NAME QmlPlugins
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Screen"
)

target_link_libraries(cutefish-framework-screen PUBLIC
    Cutefish::Display
    Qt6::Core
    Qt6::DBus
    Qt6::Quick
    Qt6::Gui
)

cutefish_install_qml_module(cutefish-framework-screen)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(display-outputmodel-test tests/outputmodel_test.cpp)
    target_link_libraries(display-outputmodel-test PRIVATE cutefish-framework-screen Qt6::Test)
    target_include_directories(display-outputmodel-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME display-outputmodel-test COMMAND display-outputmodel-test)
    add_executable(display-client-test tests/displayclient_test.cpp)
    target_link_libraries(display-client-test PRIVATE Cutefish::Display Qt6::Test)
    add_test(NAME display-client-test
        COMMAND dbus-run-session -- $<TARGET_FILE:display-client-test>)
    if(CUTEFISH_TEST_COMPOSITOR)
        add_executable(display-wayland-screen-probe tests/wayland_screen_probe.cpp)
        target_link_libraries(display-wayland-screen-probe PRIVATE Qt6::Gui)
        add_executable(display-screen-integration-test tests/screen_integration_test.cpp)
        add_dependencies(display-screen-integration-test display-wayland-screen-probe)
        target_compile_definitions(display-screen-integration-test PRIVATE
            SCREEN_PROBE_PATH="$<TARGET_FILE:display-wayland-screen-probe>")
        target_link_libraries(display-screen-integration-test PRIVATE cutefish-framework-screen Qt6::Test)
        target_include_directories(display-screen-integration-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME display-screen-integration-test
            COMMAND dbus-run-session -- $<TARGET_FILE:display-screen-integration-test>)
        set_tests_properties(display-screen-integration-test PROPERTIES
            ENVIRONMENT "CUTEFISH_TEST_COMPOSITOR=${CUTEFISH_TEST_COMPOSITOR}")
    endif()
endif()
