set(CUTEFISH_SCREEN_SOURCES
    displayclient.cpp
    displayclient.h
    displaytypes.cpp
    displaytypes.h
    output.cpp
    output.h
    outputmodel.cpp
    outputmodel.h
    screen.cpp
    screen.h
)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)

set(DISPLAY_CONFIG_XML ${CMAKE_CURRENT_SOURCE_DIR}/com.cutefish.DisplayConfig1.xml)
set_source_files_properties(${DISPLAY_CONFIG_XML} PROPERTIES INCLUDE displaytypes.h)
qt_add_dbus_interface(DISPLAY_CONFIG_DBUS_SOURCES ${DISPLAY_CONFIG_XML} display_config_interface)

add_library(cutefish-framework-screen SHARED ${CUTEFISH_SCREEN_SOURCES} ${DISPLAY_CONFIG_DBUS_SOURCES})
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
