cmake_minimum_required(VERSION 3.16)

add_executable(credstore-test main.cpp)

target_include_directories(credstore-test PRIVATE
    ../../engine/src
)

# Link against the engine library which already includes agentcredentialstore
# and has HAVE_QTKEYCHAIN defined + qt6keychain linked when available.
# macOS requires QApplication (Widgets) for qtkeychain's GCD integration.
target_link_libraries(credstore-test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Widgets
    qlcplusengine
)
