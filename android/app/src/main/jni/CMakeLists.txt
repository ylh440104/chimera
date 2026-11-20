cmake_minimum_required(VERSION 3.22.1)

project(chimera C ASM)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

add_library(chimera SHARED
        chimera_jni.c
        ${ROOT}/src/macho.c
        ${ROOT}/src/fixups.c
        ${ROOT}/src/session.c
        ${ROOT}/src/objc.c
        ${ROOT}/src/objc_arm64.S
        ${ROOT}/src/libcshim.c
        ${ROOT}/src/dispatch.c
        ${ROOT}/src/foundation.c
        ${ROOT}/src/frameworks.c
        ${ROOT}/src/trap.c
        ${ROOT}/src/loader.c)

target_include_directories(chimera PRIVATE ${ROOT}/include)

target_link_libraries(chimera log m dl)