find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml)

set(CUTEFISH_APPLICATIONS_SRCS
    languageenvironment.cpp
    languageenvironment.h
    applicationlauncher.cpp
    applicationlauncher.h
    applicationruntime.cpp
    applicationruntime.h
    applicationregistry.cpp
    applicationregistry.h
    desktopentry.cpp
    desktopentry.h
)

add_library(cutefish-framework-applications STATIC ${CUTEFISH_APPLICATIONS_SRCS})
add_library(Cutefish::Applications ALIAS cutefish-framework-applications)
set_target_properties(cutefish-framework-applications PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(cutefish-framework-applications PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Applications>
)
target_link_libraries(cutefish-framework-applications
    PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
)

set_target_properties(cutefish-framework-applications PROPERTIES EXPORT_NAME Applications)
install(TARGETS cutefish-framework-applications EXPORT CutefishFrameworkApplicationsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    languageenvironment.h
    applicationlauncher.h
    applicationruntime.h
    applicationregistry.h
    desktopentry.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Applications
)
install(EXPORT CutefishFrameworkApplicationsTargets
    NAMESPACE Cutefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_languageenvironment tests/tst_languageenvironment.cpp)
    target_link_libraries(tst_languageenvironment PRIVATE Cutefish::Applications Qt6::Test)
    add_test(NAME languageenvironment COMMAND tst_languageenvironment)
    add_executable(tst_applicationregistry tests/tst_applicationregistry.cpp)
    target_link_libraries(tst_applicationregistry PRIVATE Cutefish::Applications Qt6::Test)
    add_test(NAME applicationregistry COMMAND tst_applicationregistry)
endif()
