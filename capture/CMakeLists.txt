# The compositor's capture protocols as a client sees them: outputs and windows
# captured into shared memory or DMA-BUFs. The shell takes screenshots with it,
# cutefish-core's screen recorder records with it.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols>=1.39)
pkg_check_modules(Gbm REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(Libdrm REQUIRED IMPORTED_TARGET libdrm)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)

add_library(cutefish-framework-capture SHARED
    captureclient.cpp
    captureclient.h
    capturesession.cpp
    capturesession.h
)
add_library(Cutefish::Capture ALIAS cutefish-framework-capture)

# Generated privately: nothing in the installed headers needs them.
set(protocol_dir ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${protocol_dir})
foreach(protocol
        staging/ext-image-capture-source/ext-image-capture-source-v1
        staging/ext-image-copy-capture/ext-image-copy-capture-v1
        staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1
        stable/linux-dmabuf/linux-dmabuf-v1
        unstable/xdg-output/xdg-output-unstable-v1)
    get_filename_component(name ${protocol} NAME)
    set(xml ${WAYLAND_PROTOCOLS_DATADIR}/${protocol}.xml)
    set(header ${protocol_dir}/${name}-client-protocol.h)
    set(code ${protocol_dir}/${name}-protocol.c)
    add_custom_command(OUTPUT ${header}
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${xml} ${header}
        DEPENDS ${xml} VERBATIM)
    add_custom_command(OUTPUT ${code}
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${xml} ${code}
        DEPENDS ${xml} VERBATIM)
    set_source_files_properties(${header} ${code} PROPERTIES SKIP_AUTOGEN ON)
    target_sources(cutefish-framework-capture PRIVATE ${header} ${code})
endforeach()

target_include_directories(cutefish-framework-capture
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Capture>
    PRIVATE ${protocol_dir}
)
target_link_libraries(cutefish-framework-capture
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE PkgConfig::WaylandClient PkgConfig::Gbm PkgConfig::Libdrm
)

set_target_properties(cutefish-framework-capture PROPERTIES EXPORT_NAME Capture)
install(TARGETS cutefish-framework-capture EXPORT CutefishFrameworkCaptureTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    captureclient.h
    capturesession.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Capture
)
install(EXPORT CutefishFrameworkCaptureTargets
    NAMESPACE Cutefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
)
