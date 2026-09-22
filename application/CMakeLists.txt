find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(cutefish-framework-application STATIC
    cutefishapplication.cpp
    cutefishapplication.h
)
add_library(Cutefish::Application ALIAS cutefish-framework-application)
set_target_properties(cutefish-framework-application PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(cutefish-framework-application PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Application>
)
target_link_libraries(cutefish-framework-application
    PUBLIC
        Qt6::Core
)

set_target_properties(cutefish-framework-application PROPERTIES EXPORT_NAME Application)
install(TARGETS cutefish-framework-application EXPORT CutefishFrameworkApplicationTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES cutefishapplication.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Application)
install(EXPORT CutefishFrameworkApplicationTargets
    NAMESPACE Cutefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
)
