# Client bindings for the private Wayland protocols the compositor speaks.
#
# The XML here must stay byte-identical to cutefish-compositor/protocols:
# changing one without the other breaks the wire format. Other projects link
# Cutefish::Protocols instead of keeping their own copy.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols>=1.39)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)

set(protocols
    cutefish-shell-v1
    cutefish-surface-blur-v1
    cutefish-window-hints-v1
    wlr-data-control-unstable-v1
)
# The ext_image_capture_source_v1 and ext_foreign_toplevel_list_v1 tables are
# exported from here as well, for clients that create capture sources.
set(protocol_xml_ext-image-capture-source-v1
    "${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml")
set(protocol_xml_ext-foreign-toplevel-list-v1
    "${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml")
list(APPEND protocols ext-foreign-toplevel-list-v1 ext-image-capture-source-v1)

set(protocol_headers)
set(protocol_sources)
foreach(protocol IN LISTS protocols)
    set(xml "${CMAKE_CURRENT_SOURCE_DIR}/${protocol}.xml")
    if(DEFINED protocol_xml_${protocol})
        set(xml "${protocol_xml_${protocol}}")
    endif()
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client-protocol.h")
    set(code "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c")
    add_custom_command(OUTPUT "${header}"
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header "${xml}" "${header}"
        DEPENDS "${xml}" VERBATIM
    )
    # public-code: the interface tables are exported from this library.
    add_custom_command(OUTPUT "${code}"
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} public-code "${xml}" "${code}"
        DEPENDS "${xml}" VERBATIM
    )
    list(APPEND protocol_headers "${header}")
    list(APPEND protocol_sources "${code}")
endforeach()

set_source_files_properties(${protocol_headers} ${protocol_sources} PROPERTIES SKIP_AUTOGEN ON)

add_library(cutefish-framework-protocols SHARED
    ${protocol_headers}
    ${protocol_sources}
)
add_library(Cutefish::Protocols ALIAS cutefish-framework-protocols)

target_include_directories(cutefish-framework-protocols PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Protocols>
)
target_link_libraries(cutefish-framework-protocols PUBLIC PkgConfig::WaylandClient)

set_target_properties(cutefish-framework-protocols PROPERTIES EXPORT_NAME Protocols)
install(TARGETS cutefish-framework-protocols EXPORT CutefishFrameworkProtocolsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES ${protocol_headers}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Protocols
)
install(EXPORT CutefishFrameworkProtocolsTargets
    NAMESPACE Cutefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
)
