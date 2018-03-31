cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(thesis)
set(PROJECT_NAME thesis)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

add_subdirectory(src/common build/common)
add_subdirectory(src/client build/client)
add_subdirectory(src/server build/server)
