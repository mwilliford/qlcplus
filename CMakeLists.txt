cmake_minimum_required(VERSION 3.16)
project(qlcplus VERSION 5.2.2 LANGUAGES C CXX)

# Set Release build type by default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

# Prevent CMake make install strips off non-standard build paths
# Refer to https://stackoverflow.com/questions/7970544/cmake-make-install-output-cant-find-shared-qt-libraries-under-redhat
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

SET(INSTALL_ROOT "/" CACHE STRING "Installation root directory")

if(UNIX)
    if (APPLE)
        set(iokit ON)
    else()
        set(udev ON)
    endif()
endif()

if (ANDROID)
    set(ANDROID_PACKAGE_STAGING_DIR "${CMAKE_BINARY_DIR}/android-package")
    # Tell Qt/Gradle to use the build-tree template
    set(QT_ANDROID_PACKAGE_SOURCE_DIR "${ANDROID_PACKAGE_STAGING_DIR}" CACHE PATH "" FORCE)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Set up AUTOMOC and some sensible defaults for runtime execution
# When using Qt 6.3, you can replace the code block below with
# qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui HttpServer Multimedia MultimediaWidgets Network PrintSupport Qml Quick QuickWidgets Svg Test Widgets LinguistTools WebSockets)
# CorePrivate exposes QZipReader/QZipWriter (Q_CORE_EXPORT'd private API) for BhxIO.
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS CorePrivate)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS 3DCore 3DInput 3DLogic 3DQuick 3DQuickExtras 3DRender)
if(ANDROID)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent OpenGL)
endif()

message("Found Qt version ${QT_VERSION_MAJOR}: ${QT_DIR}")

if(QT_VERSION_MAJOR EQUAL 5)
    find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Script)
endif()

find_package(PkgConfig REQUIRED)

include(./variables.cmake)
include(./coverage.cmake)

if((((QT_VERSION_MAJOR LESS 5)) AND (APPLE)))
    pkg_check_modules(PORTAUDIO_2 IMPORTED_TARGET portaudio-2.0)
endif()

if(WIN32)
    # Prefix all shared libraries with ''.
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif()

add_subdirectory(hotplugmonitor)
add_subdirectory(engine)
add_subdirectory(resources)
add_subdirectory(plugins)
add_subdirectory(webaccess)
add_subdirectory(render)
add_subdirectory(qmlui)

add_subdirectory(tools/credstore-test)

# Unit testing thru "make check"
if(WIN32)
   add_custom_target(unittests
        COMMAND unittest.bat ${CMAKE_CURRENT_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
else()
   add_custom_target(unittests
        COMMAND ./unittest.sh ${CMAKE_CURRENT_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

add_custom_target(check
    DEPENDS unittests
)

# Unit test coverage measurement
if(WIN32)
    add_custom_target(coverage
        COMMAND @echo Get a better OS.
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
else()
   add_custom_target(coverage
        COMMAND ./coverage.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

add_custom_target(lcov
    DEPENDS coverage
)

# Translations
add_custom_target(translations ALL
  COMMAND ./translate.sh release
  COMMENT "Translating qmlui translations..."
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

## Install the qm files
if (appimage)
  set(translations_install_path "${TARGET_DIR}/${INSTALLROOT}/${TRANSLATIONDIR}")
else ()
  set(translations_install_path "${INSTALLROOT}/${TRANSLATIONDIR}")
endif()

# Define the pattern for the .qm files
set(translations_qm_pattern "${CMAKE_CURRENT_SOURCE_DIR}/qlcplus_*.qm")

# Install command using a custom script
install(CODE "
  file(GLOB qm_files \"${translations_qm_pattern}\")
  foreach(file IN LISTS qm_files)
    file(INSTALL DESTINATION \"${translations_install_path}\" TYPE FILE FILES \"\${file}\")
  endforeach()
" COMPONENT translations)

## Clean the generated files
set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${CMAKE_CURRENT_BINARY_DIR}/*.qm")

# run
if(UNIX)
    add_custom_target(run
        COMMAND ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=engine/src:$ENV{LD_LIBRARY_PATH} qmlui/qlcplus-qml
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# doxygen
if(UNIX)
    add_custom_target(doxygen
        COMMAND cd resources/doxygen && rm -rf html/ && doxygen qlcplus.dox
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

SET(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "qlcplus")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Massimo Callegari") #required
INCLUDE(CPack)

# Leave this on the last row of this file
add_subdirectory(platforms)

# HELPER: PRINT ALL VARIABLES KNOWN BY CMAKE

#message("QTDIR: ${QT_DIR}")
#get_cmake_property(_variableNames VARIABLES)
#list (SORT _variableNames)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
