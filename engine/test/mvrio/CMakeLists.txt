add_executable(mvrio_test WIN32
    mvrio_test.cpp mvrio_test.h
)
target_include_directories(mvrio_test PRIVATE
    ../../../plugins/interfaces
    ../../src
    ${CMAKE_BINARY_DIR}/_deps/libmvrgdtf-src/src
)

target_link_libraries(mvrio_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
    MvrGdtf
)
