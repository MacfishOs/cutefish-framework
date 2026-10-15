cutefish_add_client(upower TARGET UPowerClient
    SOURCES batterystatus.cpp device.cpp
    HEADERS batterystatus.h device.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
