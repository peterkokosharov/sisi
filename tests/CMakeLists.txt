# Unit tests for the parts of Sisi that need no UI (src/ini.c, src/core.h).
# Standalone:  cmake -S tests -B build-tests && cmake --build build-tests --config Release
#              ctest --test-dir build-tests -C Release --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(sisi_tests C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    enable_testing()
endif()

add_executable(sisi_tests test_core.cpp ../src/ini.c)
target_include_directories(sisi_tests PRIVATE ../src)
if(WIN32)
    target_compile_definitions(sisi_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME sisi_tests COMMAND sisi_tests)
