cutefish_add_client(timedate TARGET TimeDateClient
    SOURCES timedate.cpp
    HEADERS timedate.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
