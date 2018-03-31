cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(thesis)
set(PROJECT_NAME thesis)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

include(${CMAKE_MODULE_PATH}/detect_arch.cmake)
include(${CMAKE_MODULE_PATH}/my_functions.cmake)

detect_arch()
message(STATUS "Target arch = ${TARGET_ARCH}bit")

add_subdirectory(src/common build/common)
add_subdirectory(src/client build/client)
add_subdirectory(src/server build/server)
