cmake_minimum_required(VERSION 3.21)
project(Flowchart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Test)

add_library(flowcore STATIC
    src/Model.h src/Model.cpp
    src/Items.h src/Items.cpp
    src/ChartScene.h src/ChartScene.cpp
    src/ChartView.h src/ChartView.cpp
    src/Panels.h src/Panels.cpp
    src/HomeScreen.h src/HomeScreen.cpp
    src/MainWindow.h src/MainWindow.cpp)
target_include_directories(flowcore PUBLIC src)
target_link_libraries(flowcore PUBLIC Qt6::Widgets Qt6::Svg)

add_executable(Flowchart WIN32 src/main.cpp)
target_link_libraries(Flowchart PRIVATE flowcore)

add_executable(selftest tests/selftest.cpp)
target_link_libraries(selftest PRIVATE flowcore)

add_executable(guitest tests/guitest.cpp)
target_link_libraries(guitest PRIVATE flowcore Qt6::Test)
