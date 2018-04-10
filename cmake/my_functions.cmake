function(do_add_compiler_flags)
	if(RELEASE)
		message(STATUS "Compiling in RELEASE mode")
		# Disable assertions
		add_definitions(-DNDEBUG=1)
		add_definitions(-DRELEASE=1)
		target_compile_options(${PROJECT_NAME} PUBLIC -D_FORTIFY_SOURCE=2 -O2 -Wall -pedantic)
	else()
		message(STATUS "Compiling in DEBUG mode")
		target_compile_options(${PROJECT_NAME} PUBLIC -O0 -ggdb -Wall -pedantic)
	endif()
endfunction(do_add_compiler_flags)

function(do_add_linker_flags)
	# Linker
	execute_process(
		COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version
		ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
	if("${LD_VERSION}" MATCHES "GNU gold")
		target_compile_options(${PROJECT_NAME} PUBLIC -fuse-ld=gold)
		message(STATUS "Using GNU gold as linker")
	endif()
endfunction(do_add_linker_flags)

function(do_build)

	do_add_compiler_flags()

	# Profiling tools
	if(${GPROF})
		target_compile_options(${PROJECT_NAME} PUBLIC -pg -no-pie -fPIC)
		message(STATUS "Compiling with gprof support")
	endif()

	do_add_linker_flags()

endfunction(do_build)
