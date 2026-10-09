find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml Test)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

# network_export.h
include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

add_executable(tst_wifiutils tst_wifiutils.cpp)
target_link_libraries(tst_wifiutils PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-wifiutils COMMAND tst_wifiutils)

add_executable(tst_vpnutils tst_vpnutils.cpp)
target_link_libraries(tst_vpnutils PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-vpnutils COMMAND tst_vpnutils)

add_executable(tst_iputils tst_iputils.cpp)
target_link_libraries(tst_iputils PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-iputils COMMAND tst_iputils)

add_executable(tst_vpnschema tst_vpnschema.cpp)
target_link_libraries(tst_vpnschema PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-vpnschema COMMAND tst_vpnschema)

add_executable(tst_networkclient tst_networkclient.cpp fakenetworkmanager.h fixture.h)
target_link_libraries(tst_networkclient PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-client COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_networkclient>)
set_tests_properties(network-client PROPERTIES TIMEOUT 60)

add_executable(tst_secretagent tst_secretagent.cpp fakenetworkmanager.h fakesecrets.h)
target_link_libraries(tst_secretagent PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-secretagent COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_secretagent>)
set_tests_properties(network-secretagent PROPERTIES TIMEOUT 60)
