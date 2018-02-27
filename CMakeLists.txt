cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(thesis)
set(PROJECT_NAME thesis)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(MAIN_FILE main.cpp)

include_directories(src/)
include_directories(src/third_party)

file(GLOB SRC ${MAIN_FILE}
	src/*.cpp
	src/third_party/*.cpp
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
include_directories(${GLFW_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GLFW_LIBRARIES})

find_package(Sparsehash REQUIRED)
include_directories(${Sparsehash_INCLUDE_DIRS})

#####################

if(RELEASE)
	message(STATUS "Compiling in RELEASE mode")
	# Disable assertions
	add_definitions(-DNDEBUG=1)
	add_definitions(-DRELEASE=1)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FORTIFY_SOURCE=2 -O2 -Wall -pedantic -Wextra")
else()
	message(STATUS "Compiling in DEBUG mode")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -ggdb -Wall -pedantic -Wextra")
endif()

if(NO_EX_TINYOBJ)
	message(STATUS "Using STABLE tinyobj")
else()
	message(STATUS "Using EXPERIMENTAL tinyobj")
	add_definitions(-DUSE_EXPERIMENTAL_TINYOBJ=1)
endif()

# Linker
execute_process(
	COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version
	ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
if("${LD_VERSION}" MATCHES "GNU gold")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
	message(STATUS "Using GNU gold as linker")
endif()
