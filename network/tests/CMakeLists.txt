find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml Test)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

add_executable(tst_secretagent tst_secretagent.cpp fakesecrets.h)
target_link_libraries(tst_secretagent PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-secretagent COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_secretagent>)
set_tests_properties(network-secretagent PROPERTIES TIMEOUT 60)

add_executable(tst_networkmanager tst_networkmanager.cpp fakenetworkmanager.h)
target_include_directories(tst_networkmanager PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)
target_link_libraries(tst_networkmanager PRIVATE cutefish-framework-network Qt6::Test)
add_test(NAME network-manager COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_networkmanager>)
set_tests_properties(network-manager PROPERTIES TIMEOUT 60)
