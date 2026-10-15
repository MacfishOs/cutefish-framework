find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(timedate-client-tests timedate_test.cpp)
target_link_libraries(timedate-client-tests PRIVATE Cutefish::TimeDateClient GTest::gtest_main)
gtest_discover_tests(timedate-client-tests)
