find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(accountsmanager_test accountsmanager_test.cpp)
target_include_directories(accountsmanager_test PRIVATE ..)
target_link_libraries(accountsmanager_test PRIVATE
    Cutefish::Accounts
    Qt6::DBus
    Qt6::Test
)

find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
if(DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME accountsmanager_test
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:accountsmanager_test>)
endif()
