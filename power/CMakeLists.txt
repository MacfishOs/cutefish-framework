find_package(Qt6 REQUIRED COMPONENTS Core DBus)

add_library(cutefish-framework-power SHARED
    batterystatus.cpp
    batterystatus.h
)
add_library(Cutefish::Power ALIAS cutefish-framework-power)

target_include_directories(cutefish-framework-power PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Power>
)
target_link_libraries(cutefish-framework-power PUBLIC
    Qt6::Core
    Qt6::DBus
)

set_target_properties(cutefish-framework-power PROPERTIES EXPORT_NAME Power)
install(TARGETS cutefish-framework-power EXPORT CutefishFrameworkPowerTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    batterystatus.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Power
)
install(EXPORT CutefishFrameworkPowerTargets
    NAMESPACE Cutefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(batterystatus-test tests/batterystatus_test.cpp)
    target_link_libraries(batterystatus-test PRIVATE Cutefish::Power Qt6::Test)
    add_test(NAME batterystatus-test
        COMMAND dbus-run-session -- $<TARGET_FILE:batterystatus-test>)
endif()
