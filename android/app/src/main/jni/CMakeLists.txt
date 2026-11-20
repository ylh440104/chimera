cmake_minimum_required(VERSION 3.22.1)

project(chimera C)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

file(GLOB CHM_SOURCES ${ROOT}/src/*.c)

add_library(chimera SHARED
        chimera_jni.c
        ${CHM_SOURCES})

target_include_directories(chimera PRIVATE ${ROOT}/include)

target_link_libraries(chimera log)