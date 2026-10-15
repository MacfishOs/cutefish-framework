cutefish_add_client(display TARGET DisplayClient
    SOURCES
        displayclient.cpp
        nightlight.cpp
        outputmodel.cpp
        screen.cpp
    HEADERS
        displayclient.h
        displaytypes.h
        nightlight.h
        outputmodel.h
        screen.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
