find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(powerprofiles-client-tests powerprofiles_test.cpp)
target_link_libraries(powerprofiles-client-tests PRIVATE Cutefish::PowerProfilesClient GTest::gtest_main)
gtest_discover_tests(powerprofiles-client-tests)
