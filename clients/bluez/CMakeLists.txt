cutefish_add_client(bluez TARGET BluezClient
    SOURCES
        bluetoothobject.cpp
        devicesmodel.cpp
        manager.cpp
    HEADERS
        bluetoothobject.h
        devicesmodel.h
        manager.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
