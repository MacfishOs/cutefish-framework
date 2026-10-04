find_package(Qt6 REQUIRED COMPONENTS Gui Quick Widgets)
find_package(Qt6Gui CONFIG QUIET COMPONENTS Private)
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
find_package(Qt6Quick CONFIG QUIET COMPONENTS Private)
if(NOT TARGET Qt6::QuickPrivate)
    find_package(Qt6 REQUIRED COMPONENTS QuickPrivate)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)

include(GNUInstallDirs)

# Standard protocols are generated privately, so their symbols cannot clash
# with the copies inside Qt.
set(protocol_dir ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${protocol_dir})
set(pointer_gestures_xml
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/pointer-gestures/pointer-gestures-unstable-v1.xml)
set(pointer_gestures_header ${protocol_dir}/pointer-gestures-unstable-v1-client-protocol.h)
set(pointer_gestures_code ${protocol_dir}/pointer-gestures-unstable-v1-protocol.c)
add_custom_command(OUTPUT ${pointer_gestures_header}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${pointer_gestures_xml} ${pointer_gestures_header}
    DEPENDS ${pointer_gestures_xml} VERBATIM
)
add_custom_command(OUTPUT ${pointer_gestures_code}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${pointer_gestures_xml} ${pointer_gestures_code}
    DEPENDS ${pointer_gestures_xml} VERBATIM
)
set_source_files_properties(${pointer_gestures_header} ${pointer_gestures_code} PROPERTIES SKIP_AUTOGEN ON)

# The physics and the hold gesture, for any kind of view.
add_library(cutefish-framework-scrolling SHARED
    scroller.cpp
    scroller.h
    scrollhold.cpp
    scrollhold.h
    ${pointer_gestures_header}
    ${pointer_gestures_code}
)
add_library(Cutefish::Scrolling ALIAS cutefish-framework-scrolling)
target_include_directories(cutefish-framework-scrolling
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish>
    PRIVATE
        ${protocol_dir}
)
target_link_libraries(cutefish-framework-scrolling
    PUBLIC
        Qt6::Gui
    PRIVATE
        Qt6::GuiPrivate
        PkgConfig::WaylandClient
)
set_target_properties(cutefish-framework-scrolling PROPERTIES EXPORT_NAME Scrolling)

# Qt Quick views. A library of its own so widget applications never load Qt Quick.
add_library(cutefish-framework-scrolling-quick SHARED
    flickablescroller.cpp
    flickablescroller.h
    quickscrolling.cpp
    quickscrolling.h
)
add_library(Cutefish::ScrollingQuick ALIAS cutefish-framework-scrolling-quick)
target_link_libraries(cutefish-framework-scrolling-quick
    PUBLIC
        Cutefish::Scrolling
        Qt6::Quick
    PRIVATE
        Qt6::QuickPrivate
)
set_target_properties(cutefish-framework-scrolling-quick PROPERTIES EXPORT_NAME ScrollingQuick)

add_library(cutefish-framework-scrolling-widgets SHARED
    scrollareascroller.cpp
    scrollareascroller.h
    widgetscrolling.cpp
    widgetscrolling.h
)
add_library(Cutefish::ScrollingWidgets ALIAS cutefish-framework-scrolling-widgets)
target_link_libraries(cutefish-framework-scrolling-widgets
    PUBLIC
        Cutefish::Scrolling
        Qt6::Widgets
)
set_target_properties(cutefish-framework-scrolling-widgets PROPERTIES EXPORT_NAME ScrollingWidgets)

install(TARGETS cutefish-framework-scrolling EXPORT CutefishFrameworkScrollingTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS cutefish-framework-scrolling-quick EXPORT CutefishFrameworkScrollingQuickTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS cutefish-framework-scrolling-widgets EXPORT CutefishFrameworkScrollingWidgetsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    flickablescroller.h
    quickscrolling.h
    scrollareascroller.h
    scroller.h
    scrollhold.h
    widgetscrolling.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish
)
foreach(component Scrolling ScrollingQuick ScrollingWidgets)
    install(EXPORT CutefishFramework${component}Targets
        NAMESPACE Cutefish::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
    )
endforeach()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
