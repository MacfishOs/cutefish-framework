find_package(Qt6 REQUIRED COMPONENTS Core Qml)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3>=0.3.65)
pkg_check_modules(SPA REQUIRED IMPORTED_TARGET libspa-0.2)

set(CUTEFISH_PRIVACY_SOURCES
    deviceuse.h
    deviceusage.cpp
    deviceusage.h
    pipewirecapture.cpp
    pipewirecapture.h
    videodevicewatch.cpp
    videodevicewatch.h
    qml/plugin.cpp
)

add_library(cutefish-framework-privacy SHARED ${CUTEFISH_PRIVACY_SOURCES})
add_library(Cutefish::Privacy ALIAS cutefish-framework-privacy)

qt_add_qml_module(cutefish-framework-privacy
    URI Cutefish.Privacy
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-privacy
    CLASS_NAME Plugin
    NO_GENERATE_PLUGIN_SOURCE
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Privacy"
)

target_include_directories(cutefish-framework-privacy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutefish-framework-privacy PUBLIC
    Qt6::Core
    Qt6::Qml
    PkgConfig::PIPEWIRE
    PkgConfig::SPA
)

cutefish_install_qml_module(cutefish-framework-privacy)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
