function(do_add_compiler_flags)
	if(RELEASE)
		message(STATUS "Compiling in RELEASE mode")
		# Disable assertions
		add_definitions(-DNDEBUG=1)
		add_definitions(-DRELEASE=1)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FORTIFY_SOURCE=2 -O2 -Wall -pedantic")
	else()
		message(STATUS "Compiling in DEBUG mode")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -ggdb -Wall -pedantic")
	endif()
endfunction(do_add_compiler_flags)

function(do_add_linker_flags)
	# Linker
	execute_process(
		COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version
		ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
	if("${LD_VERSION}" MATCHES "GNU gold")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
		message(STATUS "Using GNU gold as linker")
	endif()
endfunction(do_add_linker_flags)

function(do_build)
	
	do_add_compiler_flags()

	# Profiling tools
	if(${GPROF})
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg -no-pie -fPIC")
		message(STATUS "Compiling with gprof support")
	endif()

	do_add_linker_flags()

endfunction(do_build)
