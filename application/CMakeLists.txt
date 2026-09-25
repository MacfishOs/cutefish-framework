find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)

add_library(cutefish-framework-application SHARED
    cutefishapplication.cpp
    cutefishapplication.h
    languageenvironment.cpp
    languageenvironment.h
    keyfile.cpp
    keyfile.h
    proxysettings.cpp
    proxysettings.h
    applicationlauncher.cpp
    applicationlauncher.h
    applicationruntime.cpp
    applicationruntime.h
    applicationregistry.cpp
    applicationregistry.h
    desktopentry.cpp
    desktopentry.h
    windowhints.cpp
    windowhints.h
    waylandwindowhints.cpp
    waylandwindowhints_p.h
)
add_library(Cutefish::Application ALIAS cutefish-framework-application)

target_include_directories(cutefish-framework-application PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Application>
)
target_link_libraries(cutefish-framework-application
    PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
    PRIVATE
        Qt6::GuiPrivate
        PkgConfig::WaylandClient
        Cutefish::Protocols
)

set_target_properties(cutefish-framework-application PROPERTIES EXPORT_NAME Application)
install(TARGETS cutefish-framework-application EXPORT CutefishFrameworkApplicationTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    cutefishapplication.h
    languageenvironment.h
    keyfile.h
    proxysettings.h
    applicationlauncher.h
    applicationruntime.h
    applicationregistry.h
    desktopentry.h
    windowhints.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish/Application
)
install(EXPORT CutefishFrameworkApplicationTargets
    NAMESPACE Cutefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFramework
)

qt_extract_metatypes(cutefish-framework-application)
qt_add_qml_module(cutefish-framework-application-qml
    URI Cutefish.Application
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-application-qml
    CLASS_NAME ApplicationQmlPlugin
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Application"
    SOURCES
        qmlregistration.cpp
        qmlregistration.h
)
target_link_libraries(cutefish-framework-application-qml PRIVATE
    Cutefish::Application
    Qt6::Qml
)
cutefish_install_qml_module(cutefish-framework-application-qml)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test Quick)

    add_executable(tst_languageenvironment tests/tst_languageenvironment.cpp)
    target_link_libraries(tst_languageenvironment PRIVATE Cutefish::Application Qt6::Test)
    add_test(NAME languageenvironment COMMAND tst_languageenvironment)

    add_executable(tst_proxysettings tests/tst_proxysettings.cpp)
    target_link_libraries(tst_proxysettings PRIVATE Cutefish::Application Qt6::Test)
    add_test(NAME proxysettings COMMAND tst_proxysettings)

    add_executable(tst_applicationregistry tests/tst_applicationregistry.cpp)
    target_link_libraries(tst_applicationregistry PRIVATE Cutefish::Application Qt6::Test)
    add_test(NAME applicationregistry COMMAND tst_applicationregistry)

    add_executable(applicationqml-test tests/tst_applicationqml.cpp)
    target_link_libraries(applicationqml-test PRIVATE Cutefish::Application Qt6::Test)
    target_compile_definitions(applicationqml-test PRIVATE
        APPLICATION_QML_PATH="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(applicationqml-test cutefish-framework-application-qml)
    add_test(NAME applicationqml COMMAND applicationqml-test)

    add_executable(windowhints-test tests/tst_windowhints.cpp)
    target_link_libraries(windowhints-test PRIVATE Cutefish::Application Qt6::Test Qt6::Quick)
    target_compile_definitions(windowhints-test PRIVATE
        APPLICATION_QML_PATH="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(windowhints-test cutefish-framework-application-qml)
    add_test(NAME windowhints COMMAND windowhints-test -platform offscreen)
endif()
