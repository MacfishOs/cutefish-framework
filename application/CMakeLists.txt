find_package(Qt6 REQUIRED COMPONENTS Core QuickControls2)

add_library(cutefish-framework-application STATIC
    cutefishapplication.cpp
    cutefishapplication.h
)
add_library(Cutefish::Application ALIAS cutefish-framework-application)
set_target_properties(cutefish-framework-application PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(cutefish-framework-application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutefish-framework-application
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::QuickControls2
)
