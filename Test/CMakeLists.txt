cmake_minimum_required(VERSION 3.16)
project(Test VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Sql Network Mqtt)

qt_standard_project_setup(REQUIRES 6.10)


qt_add_executable(appTest

    sqlmanagerwrapper.h
    sqlmanagerwrapper.cpp
    mqttdbtestbridge.h
    mqttdbtestbridge.cpp
     main.cpp
)


qt_add_qml_module(appTest
    URI Test
    QML_FILES Main.qml
    SOURCES mqttmanager.cpp mqttmanager.h
)

# 属性配置
set_target_properties(appTest PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    # 统一输出目录
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_link_libraries(appTest
    PRIVATE
        Qt6::Quick
        Qt6::Core
        Qt6::Sql
        Qt6::Mqtt
        dbManager  # 你的数据库库
)

install(TARGETS appTest
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
