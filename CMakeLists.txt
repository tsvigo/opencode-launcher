cmake_minimum_required(VERSION 3.16)
project(opencode-launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Пробуем сначала Qt6, если не найден — откатываемся на Qt5.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(opencode-launcher main.cpp)

target_link_libraries(opencode-launcher PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
