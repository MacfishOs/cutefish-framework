include(CMakePackageConfigHelpers)

# Headers are included as <cutefish/<module>/...>: installed under
# ${CMAKE_INSTALL_INCLUDEDIR}/cutefish/<module>, and in the build tree through
# a link from include/cutefish/<module> to the module's source directory.
set(CUTEFISH_FRAMEWORK_INCLUDE_DIR ${PROJECT_BINARY_DIR}/include)

# cutefish_framework_add_library(<library> TARGET <Target> MODULE <module> EXPORT <set>
#     SOURCES ... [HEADERS ...] [PUBLIC_LIBRARIES ...] [PRIVATE_LIBRARIES ...])
# builds lib<library> as Cutefish::<Target> into the export set <set>.
# HEADERS are the public ones, relative to the current source directory.
function(cutefish_framework_add_library library)
    cmake_parse_arguments(arg "" "TARGET;MODULE;EXPORT" "SOURCES;HEADERS;PUBLIC_LIBRARIES;PRIVATE_LIBRARIES" ${ARGN})

    add_library(${library} SHARED ${arg_SOURCES} ${arg_HEADERS})
    add_library(Cutefish::${arg_TARGET} ALIAS ${library})

    set_target_properties(${library} PROPERTIES
        EXPORT_NAME ${arg_TARGET}
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        AUTOMOC OFF
        AUTORCC OFF
    )

    set(link ${CUTEFISH_FRAMEWORK_INCLUDE_DIR}/cutefish/${arg_MODULE})
    if(NOT EXISTS ${link})
        file(MAKE_DIRECTORY ${CUTEFISH_FRAMEWORK_INCLUDE_DIR}/cutefish)
        file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR} ${link} SYMBOLIC)
    endif()

    target_include_directories(${library}
        PUBLIC
            $<BUILD_INTERFACE:${CUTEFISH_FRAMEWORK_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/common
    )
    target_link_libraries(${library}
        PUBLIC ${arg_PUBLIC_LIBRARIES}
        PRIVATE ${arg_PRIVATE_LIBRARIES}
    )

    install(TARGETS ${library} EXPORT ${arg_EXPORT}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    foreach(header ${arg_HEADERS})
        get_filename_component(directory ${header} DIRECTORY)
        install(FILES ${header} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cutefish/${arg_MODULE}/${directory})
    endforeach()
endfunction()

# cutefish_framework_export(<Package> <set>) installs the export set <set> into
# <Package>, and writes it into the build tree's package too.
function(cutefish_framework_export package set)
    install(EXPORT ${set}
        NAMESPACE Cutefish::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${package}
    )
    export(EXPORT ${set}
        NAMESPACE Cutefish::
        FILE ${PROJECT_BINARY_DIR}/cmake/${package}/${set}.cmake
    )
endfunction()

# cutefish_framework_add_package(<Package>) installs <Package>Config.cmake,
# configured from <Package>Config.cmake.in in the current source directory,
# with its version file. The build tree holds the same package in
# cmake/<Package>, for projects built against the framework before it is
# installed: <Package>_DIR=<build>/cmake/<Package>.
function(cutefish_framework_add_package package)
    set(directory ${PROJECT_BINARY_DIR}/cmake/${package})

    configure_package_config_file(${package}Config.cmake.in
        ${directory}/${package}Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${package}
    )
    write_basic_package_version_file(${directory}/${package}ConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${directory}/${package}Config.cmake
        ${directory}/${package}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${package}
    )
endfunction()
