add_executable(calibrationmodel_test WIN32
    ../common/resource_paths.h
    calibrationmodel_test.cpp calibrationmodel_test.h
)
target_include_directories(calibrationmodel_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)
target_compile_definitions(calibrationmodel_test PRIVATE
    FIXTUREDIR="${CMAKE_SOURCE_DIR}/resources/fixtures/"
    SAMPLEDIR="${CMAKE_SOURCE_DIR}/resources/samples/"
)

target_link_libraries(calibrationmodel_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
