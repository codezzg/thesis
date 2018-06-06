cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(common)
set(PROJECT_NAME common)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

include_directories(.)
include_directories(..)

file(GLOB SRC ${MAIN_FILE}
	./*.cpp
	../third_party/*.cpp
)

add_library(${PROJECT_NAME} STATIC ${SRC})
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

find_package(Threads REQUIRED)
include_directories(${Threads_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${Threads_LIBRARIES})

find_package(GLM REQUIRED)
include_directories(${GLM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GLM_LIBRARIES})
add_definitions(-DGLM_FORCE_CXX14)

if(WIN32)
	target_link_libraries(${PROJECT_NAME} Shlwapi)
	target_link_libraries(${PROJECT_NAME} ws2_32)
endif()
#####################

do_build()
