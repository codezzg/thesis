cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(client)
set(PROJECT_NAME client)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(MAIN_FILE client.cpp)
#set(MAIN_FILE main.cpp)

include_directories(.)
include_directories(..)
include_directories(../common)

file(GLOB SRC ${MAIN_FILE}
	./*.cpp
)

add_executable(${PROJECT_NAME} ${SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED YES
	CXX_EXTENSIONS NO
	LINKER_LANGUAGE CXX
)

##### FIND PACKAGES

find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARIES})

find_package(GLFW REQUIRED)
include_directories(${GLFW_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} ${GLFW_LIBRARIES})

#find_package(Sparsehash REQUIRED)
#include_directories(${Sparsehash_INCLUDE_DIRS})

find_package(Threads REQUIRED)
include_directories(${Threads_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${Threads_LIBRARIES})

find_package(GLM REQUIRED)
include_directories(${GLM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GLM_LIBRARIES})
add_definitions(-DGLM_FORCE_CXX14)


#####################

target_link_libraries(${PROJECT_NAME} common)

do_build()
