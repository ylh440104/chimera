cmake_minimum_required(VERSION 3.22.1)

project(chimera C)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

add_library(chimera SHARED
        chimera_jni.c
        ${ROOT}/src/macho.c
        ${ROOT}/src/shim.c
        ${ROOT}/src/loader.c)

target_include_directories(chimera PRIVATE ${ROOT}/include)

target_link_libraries(chimera log)