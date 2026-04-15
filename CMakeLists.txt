cmake_minimum_required(VERSION 3.19)
project(Reine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 统一查找所有需要的包（包括 Mqtt）
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Network Widgets Quick Mqtt)
qt_standard_project_setup()

# 2. 定义全局统一的输出目录变量
# 这样所有子目录生成的 .exe 和 .dll 都会进到 build/bin 文件夹里
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 3. 添加子目录
add_subdirectory(dbManager)
add_subdirectory(Test)
add_subdirectory(TestWidget)
