find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(accounts-client-tests accounts_test.cpp)
target_link_libraries(accounts-client-tests PRIVATE Cutefish::AccountsClient GTest::gtest_main)
gtest_discover_tests(accounts-client-tests)
