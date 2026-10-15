find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3>=0.3.65)
pkg_check_modules(SPA REQUIRED libspa-0.2)

cutefish_add_client(audio TARGET AudioClient
    SOURCES
        audiomodel.cpp
        audioobject.cpp
        backend.cpp
        volumemonitor.cpp
    HEADERS
        audiomodel.h
        audioobject.h
        backend.h
        volumemonitor.h
)
# By path and name rather than as imported targets, which the installed package would have to recreate.
target_include_directories(cutefish-audio-client PUBLIC ${PIPEWIRE_INCLUDE_DIRS} ${SPA_INCLUDE_DIRS})
target_link_libraries(cutefish-audio-client PUBLIC ${PIPEWIRE_LIBRARIES})

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
