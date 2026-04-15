cmake_minimum_required(VERSION 3.16)
project(dbManager LANGUAGES CXX)


set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)


find_package(Qt6 REQUIRED COMPONENTS Core Sql Network)

# 构建共享库
add_library(dbManager SHARED
    dbManager_global.h
    dbmanager.cpp
    dbmanager.h
)

target_include_directories(dbManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接完整Qt依赖
target_link_libraries(dbManager
    PUBLIC
        Qt6::Core
        Qt6::Sql
        Qt6::Network
)

target_compile_definitions(dbManager PRIVATE DBMANAGER_LIBRARY)

set_target_properties(dbManager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)