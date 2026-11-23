cmake_minimum_required(VERSION 3.22)
project(rbhelper)
add_library(rbhelper SHARED rbhelper.cpp)
target_link_libraries(rbhelper EGL GLESv2 android log)
