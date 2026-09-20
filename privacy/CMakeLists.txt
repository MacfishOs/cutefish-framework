find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(cutefish-framework-privacy STATIC
    screenrecordingpermissions.cpp
    screenrecordingpermissions.h
)
add_library(Cutefish::Privacy ALIAS cutefish-framework-privacy)
set_target_properties(cutefish-framework-privacy PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(cutefish-framework-privacy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutefish-framework-privacy PUBLIC Qt6::Core)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_screenrecordingpermissions tests/tst_screenrecordingpermissions.cpp)
    target_link_libraries(tst_screenrecordingpermissions PRIVATE Cutefish::Privacy Qt6::Test)
    add_test(NAME screenrecordingpermissions COMMAND tst_screenrecordingpermissions)
endif()
