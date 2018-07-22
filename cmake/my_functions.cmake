function(do_add_compiler_flags)
	if(RELEASE)
		message(STATUS "${PROJECT_NAME}: Compiling in RELEASE mode")
		# Disable assertions
		add_definitions(-DNDEBUG=1)
		if(MSVC)
			target_compile_options(${PROJECT_NAME} PUBLIC /O2 /W2 /MP)
		else()
			target_compile_options(${PROJECT_NAME} PUBLIC -D_FORTIFY_SOURCE=2 -O2 -Wall -pedantic)
		endif()
	else()
		message(STATUS "${PROJECT_NAME}: Compiling in DEBUG mode")
		if(MSVC)
			target_compile_options(${PROJECT_NAME} PUBLIC /Od /W2 /MP)
		else()
			target_compile_options(${PROJECT_NAME} PUBLIC -O0 -ggdb -Wall -pedantic -Wextra)

			if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
				target_compile_options(${PROJECT_NAME} PUBLIC -Wno-missing-braces)
			endif()

			if(GPROF)
				message(STATUS "${PROJECT_NAME}: Adding GPROF support")
				target_compile_options(${PROJECT_NAME} PUBLIC -pg -no-pie)
			elseif(PPROF)
				do_add_pprof_support()
			endif()
		endif()
	endif()
endfunction(do_add_compiler_flags)

function(do_add_pprof_support)
	message(STATUS "${PROJECT_NAME}: Adding PPROF support")
	find_package(Gperftools)
	if(GPERFTOOLS_FOUND)
		target_link_libraries(${PROJECT_NAME} ${GPERFTOOLS_PROFILER})
		target_compile_options(${PROJECT_NAME} PUBLIC -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free)
	endif()
endfunction(do_add_pprof_support)

function(do_add_linker_flags)
	# Linker
	execute_process(
		COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version
		ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
	if("${LD_VERSION}" MATCHES "GNU gold")
		target_compile_options(${PROJECT_NAME} PUBLIC -fuse-ld=gold)
		message(STATUS "${PROJECT_NAME}: Using GNU gold as linker")
	endif()
endfunction(do_add_linker_flags)

function(do_build)

	do_add_compiler_flags()

	do_add_linker_flags()

endfunction(do_build)
