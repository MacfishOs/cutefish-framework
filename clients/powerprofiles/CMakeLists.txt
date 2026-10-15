cutefish_add_client(powerprofiles TARGET PowerProfilesClient
    SOURCES powerprofiles.cpp
    HEADERS powerprofiles.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
