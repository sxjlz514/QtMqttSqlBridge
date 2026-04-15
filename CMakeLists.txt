cmake_minimum_required(VERSION 3.19)
project(Reine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Network)

qt_standard_project_setup()

qt_add_executable(Reine

)

target_link_libraries(Reine
    PRIVATE
        Qt::Core
)

install(TARGETS Reine
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
add_subdirectory(dbManager)
add_subdirectory(Test)
add_subdirectory(TestWidget)