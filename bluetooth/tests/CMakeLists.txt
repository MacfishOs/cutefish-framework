cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CutefishBluetoothTests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_AUTOMOC ON)
    enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml Test)
if(NOT TARGET Cutefish::Application)
    find_package(CutefishFramework REQUIRED COMPONENTS Application)
endif()
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_executable(tst_bluetooth tst_bluetooth.cpp
    ../bluetoothmanager.cpp ../bluetoothmanager.h
    ../bluetoothobject.cpp ../bluetoothobject.h
    ../bluetoothdevicesmodel.cpp ../bluetoothdevicesmodel.h
    ../bluetoothagent.cpp ../bluetoothagent.h
    ../bluetoothrfkill.cpp ../bluetoothrfkill.h
    ../bluetoothdbustypes.cpp ../bluetoothdbustypes.h
)
target_link_libraries(tst_bluetooth PRIVATE Cutefish::Application Qt6::Core Qt6::DBus Qt6::Qml
    Qt6::Test)
add_test(NAME bluetooth COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_bluetooth>)
set_tests_properties(bluetooth PROPERTIES TIMEOUT 60)

set(settings_page "${CMAKE_CURRENT_SOURCE_DIR}/../../../settings/src/qml/Bluetooth/Main.qml")
if(TARGET cutefish-framework-bluetooth AND EXISTS "${settings_page}")
    find_package(Qt6 REQUIRED COMPONENTS Quick)
    add_executable(tst_bluetoothqml tst_bluetoothqml.cpp)
    target_link_libraries(tst_bluetoothqml PRIVATE cutefish-framework-bluetooth Qt6::Quick Qt6::Test)
    # An unversioned import would load the installed network plugin over the stubs.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stubs/Cutefish/Network/qmldir" "module Cutefish.Network\n")
    target_compile_definitions(tst_bluetoothqml PRIVATE
        BLUETOOTH_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}/.."
        STUB_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}/stubs"
        SETTINGS_BLUETOOTH_PAGE="${settings_page}"
    )
    add_test(NAME bluetooth-qml COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_bluetoothqml>)
    set_tests_properties(bluetooth-qml PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
