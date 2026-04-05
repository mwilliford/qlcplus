add_executable(spatialmodel_test WIN32
    ../common/resource_paths.h
    spatialmodel_test.cpp spatialmodel_test.h
)
target_include_directories(spatialmodel_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)
target_compile_definitions(spatialmodel_test PRIVATE
    FIXTUREDIR="${CMAKE_SOURCE_DIR}/resources/fixtures/"
    SAMPLEDIR="${CMAKE_SOURCE_DIR}/resources/samples/"
)

target_link_libraries(spatialmodel_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
