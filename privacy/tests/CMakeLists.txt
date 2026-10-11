find_package(Qt6 REQUIRED COMPONENTS Test)
find_program(PIPEWIRE_EXECUTABLE pipewire REQUIRED)
find_program(WIREPLUMBER_EXECUTABLE wireplumber REQUIRED)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

add_executable(privacy-test privacy_test.cpp)
target_link_libraries(privacy-test PRIVATE cutefish-framework-privacy Qt6::Test)
target_compile_definitions(privacy-test PRIVATE
    PRIVACY_TEST_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/pipewire.conf"
    PIPEWIRE_EXECUTABLE="${PIPEWIRE_EXECUTABLE}"
    WIREPLUMBER_EXECUTABLE="${WIREPLUMBER_EXECUTABLE}"
)
add_test(NAME privacy COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:privacy-test>)
set_tests_properties(privacy PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
