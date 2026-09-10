find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml)

set(CUTEFISH_BLUETOOTH_SOURCES
    bluetoothmanager.cpp
    bluetoothmanager.h
    bluetoothobject.cpp
    bluetoothobject.h
    bluetoothdevicesmodel.cpp
    bluetoothdevicesmodel.h
    bluetoothagent.cpp
    bluetoothagent.h
    bluetoothrfkill.cpp
    bluetoothrfkill.h
    bluetoothdbustypes.cpp
    bluetoothdbustypes.h
)

add_library(cutefish-framework-bluetooth SHARED ${CUTEFISH_BLUETOOTH_SOURCES})

qt_add_qml_module(cutefish-framework-bluetooth
    URI Cutefish.Bluetooth
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-bluetooth
    CLASS_NAME BluetoothPlugin
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Bluetooth"
    QML_FILES DevicesModel.qml
)

add_library(Cutefish::Bluetooth ALIAS cutefish-framework-bluetooth)

target_link_libraries(cutefish-framework-bluetooth PUBLIC
    Qt6::Core
    Qt6::Qml
    Qt6::DBus
)

cutefish_install_qml_module(cutefish-framework-bluetooth)
install(FILES DevicesModel.qml DESTINATION ${INSTALL_QMLDIR}/Cutefish/Bluetooth)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
