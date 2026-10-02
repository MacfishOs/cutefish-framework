find_package(Qt6 REQUIRED COMPONENTS Core Qml)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3>=0.3.65)
pkg_check_modules(SPA REQUIRED IMPORTED_TARGET libspa-0.2)

set(CUTEFISH_AUDIO_SOURCES
    audioobject.cpp
    audioobject.h
    audiomodel.cpp
    audiomodel.h
    backend.cpp
    backend.h
    volumemonitor.cpp
    volumemonitor.h
    qml/plugin.cpp
)

add_library(cutefish-framework-audio SHARED ${CUTEFISH_AUDIO_SOURCES})
add_library(Cutefish::Audio ALIAS cutefish-framework-audio)

qt_add_qml_module(cutefish-framework-audio
    URI Cutefish.Audio
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-audio
    CLASS_NAME Plugin
    NO_GENERATE_PLUGIN_SOURCE
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Audio"
)

target_include_directories(cutefish-framework-audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutefish-framework-audio PUBLIC
    Qt6::Core
    Qt6::Qml
    PkgConfig::PIPEWIRE
    PkgConfig::SPA
)

cutefish_install_qml_module(cutefish-framework-audio)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
