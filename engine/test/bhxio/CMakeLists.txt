add_executable(bhxio_test WIN32
    bhxio_test.cpp bhxio_test.h
)
target_include_directories(bhxio_test PRIVATE
    ../../../plugins/interfaces
    ../../src
    ${CMAKE_BINARY_DIR}/_deps/libmvrgdtf-src/src
)

# Reuses the same known-good GDTF the mvrio suite uses — lets us round-trip
# fixtures without inventing a new synthesis path here.
target_compile_definitions(bhxio_test PRIVATE
    BHXIO_TEST_GDTF_PATH="${CMAKE_BINARY_DIR}/_deps/libmvrgdtf-src/unittest/files/allWorking.gdtf"
)

target_link_libraries(bhxio_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::CorePrivate
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
