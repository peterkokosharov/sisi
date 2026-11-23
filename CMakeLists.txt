# Sisi Valkodav - Qt 6 Widgets front-end for rex. MIT licensed (see LICENSE).
cmake_minimum_required(VERSION 3.16)
project(sisi VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg)

add_executable(sisi WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/chatview.cpp
    src/chatview.h
    src/diagram.cpp
    src/diagram.h
    src/markdown.h
    src/permissiondialog.cpp
    src/permissiondialog.h
    src/rexbridge.cpp
    src/rexbridge.h
    src/config.cpp
    src/config.h
    src/core.h
    src/ini.c
    src/ini.h
    resources/sisi.qrc)
target_include_directories(sisi PRIVATE src)
target_link_libraries(sisi PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg)
if(WIN32)
    target_compile_definitions(sisi PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(sisi PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Sisi Valkodav"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.sisi.valkodav"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# Tests are a separate project (cmake -S tests); they are never packaged.
