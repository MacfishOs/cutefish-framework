find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml)

set(CUTEFISH_NETWORK_SOURCES
    networkdbustypes.cpp
    networkdbustypes.h

    networkclient.cpp
    networkclient.h

    wifiutils.cpp
    wifiutils.h

    vpnutils.cpp
    vpnutils.h

    vpnschema.cpp
    vpnschema.h

    networkmodel.cpp
    networkmodel.h

    appletproxymodel.cpp
    appletproxymodel.h

    activeconnection.cpp
    activeconnection.h

    enabledconnections.cpp
    enabledconnections.h

    handler.cpp
    handler.h

    secretagent.cpp
    secretagent.h

    secretstore.cpp
    secretstore.h

    configuration.cpp
    configuration.h

    wifisettings.cpp
    wifisettings.h

    wirelessitemsettings.cpp
    wirelessitemsettings.h

    enums.cpp
    enums.h

    qmlplugins.cpp
    qmlplugins.h
)

add_library(cutefish-framework-network SHARED ${CUTEFISH_NETWORK_SOURCES})
generate_export_header(cutefish-framework-network EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/network_export.h BASE_NAME Cutefish_Network)

qt_add_qml_module(cutefish-framework-network
    URI Cutefish.Network
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-network
    CLASS_NAME QmlPlugins
    NO_GENERATE_PLUGIN_SOURCE
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Network"
)

add_library(Cutefish::Network ALIAS cutefish-framework-network)

target_link_libraries(cutefish-framework-network
    PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
)

cutefish_install_qml_module(cutefish-framework-network)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
