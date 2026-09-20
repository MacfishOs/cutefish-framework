find_package(Qt6 REQUIRED COMPONENTS Test Quick)
find_program(PIPEWIRE_EXECUTABLE pipewire REQUIRED)
find_program(WIREPLUMBER_EXECUTABLE wireplumber REQUIRED)
find_program(PW_CLI_EXECUTABLE pw-cli REQUIRED)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

add_executable(audio-test audio_test.cpp)
target_link_libraries(audio-test PRIVATE cutefish-framework-audio Qt6::Test Qt6::Quick)
target_compile_definitions(audio-test PRIVATE
    AUDIO_TEST_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/pipewire.conf"
    AUDIO_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}/.."
    AUDIO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    PIPEWIRE_EXECUTABLE="${PIPEWIRE_EXECUTABLE}"
    WIREPLUMBER_EXECUTABLE="${WIREPLUMBER_EXECUTABLE}"
    PW_CLI_EXECUTABLE="${PW_CLI_EXECUTABLE}"
)
add_test(NAME audio-native-pipewire COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:audio-test>)
set_tests_properties(audio-native-pipewire PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fish"
)
