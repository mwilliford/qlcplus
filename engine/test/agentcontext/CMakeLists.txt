add_executable(agentcontext_test WIN32
    ../common/resource_paths.h
    agentcontext_test.cpp agentcontext_test.h
)
target_include_directories(agentcontext_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)
# Absolute path to fixture resources — works regardless of build dir layout
target_compile_definitions(agentcontext_test PRIVATE
    FIXTUREDIR="${CMAKE_SOURCE_DIR}/resources/fixtures/"
    SAMPLEDIR="${CMAKE_SOURCE_DIR}/resources/samples/"
)

target_link_libraries(agentcontext_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
