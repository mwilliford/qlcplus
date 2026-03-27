add_executable(agentintegration_test WIN32
    agentintegration_test.cpp agentintegration_test.h
)
target_include_directories(agentintegration_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

# Absolute path to fixture resources for loading built-in fixture definitions
target_compile_definitions(agentintegration_test PRIVATE
    FIXTUREDIR="${CMAKE_SOURCE_DIR}/resources/fixtures/"
    SAMPLEDIR="${CMAKE_SOURCE_DIR}/resources/samples/"
)

target_link_libraries(agentintegration_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::Network
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
