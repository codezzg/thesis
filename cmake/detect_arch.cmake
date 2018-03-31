function(detect_arch)
	# detect architecture
	if(CMAKE_SIZEOF_VOID_P MATCHES 8)
		set(TARGET_ARCH 64 PARENT_SCOPE)
	else()
		set(TARGET_ARCH 32 PARENT_SCOPE)
	endif()
endfunction(detect_arch)
