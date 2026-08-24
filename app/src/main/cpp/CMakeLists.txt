cmake_minimum_required(VERSION 3.22.1)
project(monkeyshell LANGUAGES CXX)

add_library(monkeyshell SHARED
    jni_bridge.cpp
    renderer.cpp
    grid.cpp
)

target_compile_features(monkeyshell PRIVATE cxx_std_17)
target_compile_options(monkeyshell PRIVATE -Wall -Wextra)

find_library(log-lib log)
target_link_libraries(monkeyshell
    android    # ANativeWindow_fromSurface
    EGL
    GLESv2
    ${log-lib}
)
