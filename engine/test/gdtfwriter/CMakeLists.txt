add_executable(gdtfwriter_test WIN32
    gdtfwriter_test.cpp gdtfwriter_test.h
)
target_include_directories(gdtfwriter_test PRIVATE
    ../../../plugins/interfaces
    ../../src
    ${CMAKE_BINARY_DIR}/_deps/libmvrgdtf-src/src
)

target_link_libraries(gdtfwriter_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
    MvrGdtf
)
