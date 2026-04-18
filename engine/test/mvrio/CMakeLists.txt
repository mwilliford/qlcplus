add_executable(mvrio_test WIN32
    mvrio_test.cpp mvrio_test.h
)
target_include_directories(mvrio_test PRIVATE
    ../../../plugins/interfaces
    ../../src
    ${CMAKE_BINARY_DIR}/_deps/libmvrgdtf-src/src
)

# Path to a known-good GDTF shipped with libmvrgdtf. Tests attach this to
# synthetic MVR archives to exercise the embedded-GDTF code paths.
target_compile_definitions(mvrio_test PRIVATE
    MVRIO_TEST_GDTF_PATH="${CMAKE_BINARY_DIR}/_deps/libmvrgdtf-src/unittest/files/allWorking.gdtf"
)

target_link_libraries(mvrio_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
    MvrGdtf
)
