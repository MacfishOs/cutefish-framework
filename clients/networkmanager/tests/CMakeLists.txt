find_package(GTest REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Systemd REQUIRED IMPORTED_TARGET libsystemd)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

add_executable(cutefish-networkmanager-client-tests
    iputils_test.cpp
    vpnschema_test.cpp
    vpnutils_test.cpp
    wifiutils_test.cpp
)
set_target_properties(cutefish-networkmanager-client-tests PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_link_libraries(cutefish-networkmanager-client-tests PRIVATE cutefish-networkmanager-client GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(cutefish-networkmanager-client-tests)

add_executable(cutefish-networkmanager-client-bus-tests client_test.cpp fakenetworkmanager.h)
set_target_properties(cutefish-networkmanager-client-bus-tests PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_link_libraries(cutefish-networkmanager-client-bus-tests PRIVATE cutefish-networkmanager-client PkgConfig::Systemd GTest::gtest)
# The client and the fake NetworkManager talk over a private session bus.
add_test(NAME cutefish-networkmanager-client-bus-tests
         COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:cutefish-networkmanager-client-bus-tests>)

add_executable(cutefish-networkmanager-secretreader-tests secretreader_test.cpp)
set_target_properties(cutefish-networkmanager-secretreader-tests PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_link_libraries(cutefish-networkmanager-secretreader-tests PRIVATE cutefish-networkmanager-client GTest::gtest)
# A fake Secret Service on a private session bus.
add_test(NAME cutefish-networkmanager-secretreader-tests
         COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:cutefish-networkmanager-secretreader-tests>)
