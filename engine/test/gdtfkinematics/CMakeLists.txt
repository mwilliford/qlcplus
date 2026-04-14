add_executable(gdtfkinematics_test WIN32
    gdtfkinematics_test.cpp gdtfkinematics_test.h
)
target_include_directories(gdtfkinematics_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(gdtfkinematics_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
