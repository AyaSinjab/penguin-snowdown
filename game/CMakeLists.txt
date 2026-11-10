# TSBK03 lab CMake template — reusable for lab0, lab1, lab2, lab3, lab4.
#
# Folder layout this assumes (same as the course zips):
#   TSBK03/
#     common/common/...        <- from common.zip
#     lab3/lab3/CMakeLists.txt <- this file, next to lab3.cpp
#     ...
#
# To reuse for another lab: copy this file into that lab's source folder,
# change LAB_NAME below, and make sure LAB_SOURCES lists that lab's .cpp
# file(s). Everything else (finding "common", picking the right platform
# backend, linker flags) stays the same.

cmake_minimum_required(VERSION 3.15)

set(LAB_NAME penguin_snowdown)
set(LAB_SOURCES main.cpp)

project(${LAB_NAME} CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

   set(COMMON_DIR ${CMAKE_SOURCE_DIR}/common)

if(WIN32)
    set(PLATFORM_DIR ${COMMON_DIR}/Windows)
    set(PLATFORM_SOURCES ${PLATFORM_DIR}/MicroGlut.c ${PLATFORM_DIR}/glew.c)
    set(PLATFORM_LIBS opengl32 glu32 gdi32)
elseif(APPLE)
    set(PLATFORM_DIR ${COMMON_DIR}/Mac)
    set(PLATFORM_SOURCES ${PLATFORM_DIR}/MicroGlut.m)
    set(PLATFORM_LIBS "-framework Cocoa" "-framework OpenGL")
else()
    set(PLATFORM_DIR ${COMMON_DIR}/Linux)
    set(PLATFORM_SOURCES ${PLATFORM_DIR}/MicroGlut.c)
    set(PLATFORM_LIBS GL X11 m)
endif()

add_executable(${LAB_NAME}
    ${LAB_SOURCES}
    ${COMMON_DIR}/GL_utilities.c
    ${COMMON_DIR}/LoadTGA.c
    ${PLATFORM_SOURCES}
)

target_include_directories(${LAB_NAME} PRIVATE ${COMMON_DIR} ${PLATFORM_DIR})
target_compile_definitions(${LAB_NAME} PRIVATE GL_GLEXT_PROTOTYPES)
target_link_libraries(${LAB_NAME} PRIVATE ${PLATFORM_LIBS})

# Static-link the MinGW runtime so the .exe runs without needing to ship
# libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll alongside it.
if(MINGW)
    target_link_options(${LAB_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# The program loads shaders/objects/textures via relative paths
# (e.g. "lab3.vert", "sphere.obj", "balls/0.tga"), so put the .exe next to
# them instead of inside build/ — then you can just run it from this folder.
set_target_properties(${LAB_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)
