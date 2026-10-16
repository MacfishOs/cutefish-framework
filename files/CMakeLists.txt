# Files: listing, file items, MIME sniffing, file operations with undo, trash,
# file watching and UDisks2 volumes, on the toolkit's base library alone.
# FilesThumbnails adds the freedesktop thumbnail cache and external
# thumbnailers, which decode through the toolkit's UI library.
#   find_package(CutefishFiles REQUIRED)
#   target_link_libraries(app PRIVATE Cutefish::Files Cutefish::FilesThumbnails)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Cutefish Files requires Linux")
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)

find_package(CutefishToolkit REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
# Unicode case folding for name search and MIME glob matching, as the toolkit
# already uses it.
pkg_check_modules(FilesIcu REQUIRED IMPORTED_TARGET icu-uc)
# Reads and writes the thumbnail cache's PNG text chunks.
pkg_check_modules(FilesPng REQUIRED IMPORTED_TARGET libpng)

set(CUTEFISH_FILES_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/CutefishFiles)

set(FILES_PUBLIC_HEADERS
    backgroundrelease.h
    directorylister.h
    directorymodel.h
    error.h
    fileenumerator.h
    fileitem.h
    filepath.h
    filesystemurl.h
    filewatcher.h
    localfilesystem.h
    mimetypes.h
    operationcontext.h
    task.h
    trashmonitor.h
    url.h
    volumemanager.h
)

add_library(cutefish-files SHARED
    ${FILES_PUBLIC_HEADERS}
    async_p.cpp
    async_p.h
    directorylister.cpp
    directorymodel.cpp
    error.cpp
    fileenumerator.cpp
    fileitem.cpp
    filepath.cpp
    filesystemurl.cpp
    filewatcher.cpp
    localfilesystem.cpp
    mimesniffer.cpp
    mimesniffer_p.h
    mimetypes.cpp
    operationcontext.cpp
    posix_p.h
    task.cpp
    trash.cpp
    trashdirectory.cpp
    trashdirectory_p.h
    trashmonitor.cpp
    url.cpp
    volumemanager.cpp
)
add_library(Cutefish::Files ALIAS cutefish-files)

set_target_properties(cutefish-files PROPERTIES
    EXPORT_NAME Files
    VERSION 1.0.0
    SOVERSION 1
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTORCC OFF
)
target_compile_definitions(cutefish-files PRIVATE _FILE_OFFSET_BITS=64)
target_include_directories(cutefish-files PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cutefish-files>
)
target_link_libraries(cutefish-files
    PUBLIC Cutefish::ToolkitBase Threads::Threads
    PRIVATE PkgConfig::FilesIcu
)

set(FILES_THUMBNAILS_PUBLIC_HEADERS
    thumbnails/thumbnailcache.h
)

add_library(cutefish-files-thumbnails SHARED
    ${FILES_THUMBNAILS_PUBLIC_HEADERS}
    thumbnails/md5_p.cpp
    thumbnails/md5_p.h
    thumbnails/thumbnailcache.cpp
    thumbnails/thumbnailers.cpp
    thumbnails/thumbnailers_p.h
)
add_library(Cutefish::FilesThumbnails ALIAS cutefish-files-thumbnails)

set_target_properties(cutefish-files-thumbnails PROPERTIES
    EXPORT_NAME FilesThumbnails
    VERSION 1.0.0
    SOVERSION 1
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTORCC OFF
)
target_link_libraries(cutefish-files-thumbnails
    PUBLIC Cutefish::Files Cutefish::Toolkit
    PRIVATE PkgConfig::FilesPng
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS cutefish-files cutefish-files-thumbnails EXPORT CutefishFilesTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES ${FILES_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cutefish-files/files)
install(FILES ${FILES_THUMBNAILS_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cutefish-files/files/thumbnails)
install(EXPORT CutefishFilesTargets
    NAMESPACE Cutefish::
    DESTINATION ${CUTEFISH_FILES_CMAKEDIR}
)

configure_package_config_file(CutefishFilesConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CutefishFilesConfig.cmake
    INSTALL_DESTINATION ${CUTEFISH_FILES_CMAKEDIR}
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/CutefishFilesConfigVersion.cmake
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CutefishFilesConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CutefishFilesConfigVersion.cmake
    DESTINATION ${CUTEFISH_FILES_CMAKEDIR}
)

# The build tree as a package too, for projects built against it before the
# framework is installed: CutefishFiles_DIR=<build>/files.
export(EXPORT CutefishFilesTargets
    NAMESPACE Cutefish::
    FILE ${CMAKE_CURRENT_BINARY_DIR}/CutefishFilesTargets.cmake
)
