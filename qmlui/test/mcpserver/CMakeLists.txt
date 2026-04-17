add_executable(mcpserver_test WIN32
    mcpserver_test.h
    mcpserver_test.cpp
    spatialview_stubs.h
    spatialview_stubs.cpp
    # Compile mcpserver directly (it lives in a monolithic executable, not a lib)
    ../../mcpserver.cpp
    ../../mcpserver.h
)

target_include_directories(mcpserver_test PRIVATE
    # qmlui headers (mcpserver.h, spatialviewwindow.h, calibratecontroller.h, etc.)
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    # engine headers (doc.h, fixture.h, scene.h, calibrationmodel.h, etc.)
    ${CMAKE_SOURCE_DIR}/engine/src
    # plugin interfaces (utils.h)
    ${CMAKE_SOURCE_DIR}/plugins/interfaces
)

target_compile_definitions(mcpserver_test PRIVATE
    NO_SSL
    USE_WEBSOCKET
    FIXTUREDIR="${CMAKE_SOURCE_DIR}/resources/fixtures/"
)

target_link_libraries(mcpserver_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Network
    Qt${QT_MAJOR_VERSION}::Quick
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::HttpServer
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
