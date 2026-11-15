cmake_minimum_required(VERSION 3.22.1)
project(packloader LANGUAGES C CXX)

# The engine checkout is gitignored; fetch it with phase0/engine/fetch-newblackbox.sh.
set(ENGINE_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../phase0/engine/NewBlackbox/Bcore/src/main/cpp")

add_library(dobby STATIC IMPORTED)
set_target_properties(dobby PROPERTIES
    IMPORTED_LOCATION "${ENGINE_CPP}/Dobby/${ANDROID_ABI}/libdobby.a")

add_library(xdl STATIC
    "${ENGINE_CPP}/xdl/xdl.c"
    "${ENGINE_CPP}/xdl/xdl_iterate.c"
    "${ENGINE_CPP}/xdl/xdl_linker.c"
    "${ENGINE_CPP}/xdl/xdl_lzma.c"
    "${ENGINE_CPP}/xdl/xdl_util.c")
target_include_directories(xdl PUBLIC "${ENGINE_CPP}")

add_library(packloader SHARED hookspike.cpp)
target_include_directories(packloader PRIVATE "${ENGINE_CPP}")
target_link_libraries(packloader dobby xdl log android z)
