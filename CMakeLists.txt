cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(thesis)
set(PROJECT_NAME thesis)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

include(${CMAKE_MODULE_PATH}/detect_arch.cmake)
include(${CMAKE_MODULE_PATH}/my_functions.cmake)

detect_arch()
message(STATUS "Target arch = ${TARGET_ARCH}bit")
if (${TARGET_ARCH} STREQUAL "64")
else()
	message(FATAL_ERROR "Target arch should be 64 bit. If you're 100% positive you want 32 bit, comment me and go ahead.")
endif()

if (WIN32)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
	add_definitions(-DVC_EXTRALEAN)
endif()

add_subdirectory(src/common build/common)
add_subdirectory(src/client build/client)
add_subdirectory(src/server build/server)
