cmake_minimum_required(VERSION 3.22.1)
project(canary LANGUAGES C CXX)

add_library(canary SHARED canary_jni.cpp)
target_link_libraries(canary log)
