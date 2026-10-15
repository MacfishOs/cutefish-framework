find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(upower-client-tests upower_test.cpp)
target_link_libraries(upower-client-tests PRIVATE Cutefish::UPowerClient GTest::gtest_main)
gtest_discover_tests(upower-client-tests)
