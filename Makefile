# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.4

# Default target executed when no arguments are given to make.
default_target: all

.PHONY : default_target

# Allow only one "make -f Makefile2" at a time, but pass parallelism.
.NOTPARALLEL:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/jacktommy/jack/inf/tesi/giacomo.parolini

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/jacktommy/jack/inf/tesi/giacomo.parolini

#=============================================================================
# Targets provided globally by CMake.

# Special rule for the target edit_cache
edit_cache:
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --cyan "No interactive CMake dialog available..."
	/usr/bin/cmake -E echo No\ interactive\ CMake\ dialog\ available.
.PHONY : edit_cache

# Special rule for the target edit_cache
edit_cache/fast: edit_cache

.PHONY : edit_cache/fast

# Special rule for the target rebuild_cache
rebuild_cache:
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --cyan "Running CMake to regenerate build system..."
	/usr/bin/cmake -H$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR)
.PHONY : rebuild_cache

# Special rule for the target rebuild_cache
rebuild_cache/fast: rebuild_cache

.PHONY : rebuild_cache/fast

# The main all target
all: cmake_check_build_system
	$(CMAKE_COMMAND) -E cmake_progress_start /home/jacktommy/jack/inf/tesi/giacomo.parolini/CMakeFiles /home/jacktommy/jack/inf/tesi/giacomo.parolini/CMakeFiles/progress.marks
	$(MAKE) -f CMakeFiles/Makefile2 all
	$(CMAKE_COMMAND) -E cmake_progress_start /home/jacktommy/jack/inf/tesi/giacomo.parolini/CMakeFiles 0
.PHONY : all

# The main clean target
clean:
	$(MAKE) -f CMakeFiles/Makefile2 clean
.PHONY : clean

# The main clean target
clean/fast: clean

.PHONY : clean/fast

# Prepare targets for installation.
preinstall: all
	$(MAKE) -f CMakeFiles/Makefile2 preinstall
.PHONY : preinstall

# Prepare targets for installation.
preinstall/fast:
	$(MAKE) -f CMakeFiles/Makefile2 preinstall
.PHONY : preinstall/fast

# clear depends
depend:
	$(CMAKE_COMMAND) -H$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR) --check-build-system CMakeFiles/Makefile.cmake 1
.PHONY : depend

#=============================================================================
# Target rules for targets named thesis

# Build rule for target.
thesis: cmake_check_build_system
	$(MAKE) -f CMakeFiles/Makefile2 thesis
.PHONY : thesis

# fast build rule for target.
thesis/fast:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/build
.PHONY : thesis/fast

main.o: main.cpp.o

.PHONY : main.o

# target to build an object file
main.cpp.o:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/main.cpp.o
.PHONY : main.cpp.o

main.i: main.cpp.i

.PHONY : main.i

# target to preprocess a source file
main.cpp.i:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/main.cpp.i
.PHONY : main.cpp.i

main.s: main.cpp.s

.PHONY : main.s

# target to generate assembly for a file
main.cpp.s:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/main.cpp.s
.PHONY : main.cpp.s

src/FPSCounter.o: src/FPSCounter.cpp.o

.PHONY : src/FPSCounter.o

# target to build an object file
src/FPSCounter.cpp.o:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/FPSCounter.cpp.o
.PHONY : src/FPSCounter.cpp.o

src/FPSCounter.i: src/FPSCounter.cpp.i

.PHONY : src/FPSCounter.i

# target to preprocess a source file
src/FPSCounter.cpp.i:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/FPSCounter.cpp.i
.PHONY : src/FPSCounter.cpp.i

src/FPSCounter.s: src/FPSCounter.cpp.s

.PHONY : src/FPSCounter.s

# target to generate assembly for a file
src/FPSCounter.cpp.s:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/FPSCounter.cpp.s
.PHONY : src/FPSCounter.cpp.s

src/Vertex.o: src/Vertex.cpp.o

.PHONY : src/Vertex.o

# target to build an object file
src/Vertex.cpp.o:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/Vertex.cpp.o
.PHONY : src/Vertex.cpp.o

src/Vertex.i: src/Vertex.cpp.i

.PHONY : src/Vertex.i

# target to preprocess a source file
src/Vertex.cpp.i:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/Vertex.cpp.i
.PHONY : src/Vertex.cpp.i

src/Vertex.s: src/Vertex.cpp.s

.PHONY : src/Vertex.s

# target to generate assembly for a file
src/Vertex.cpp.s:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/Vertex.cpp.s
.PHONY : src/Vertex.cpp.s

src/model.o: src/model.cpp.o

.PHONY : src/model.o

# target to build an object file
src/model.cpp.o:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/model.cpp.o
.PHONY : src/model.cpp.o

src/model.i: src/model.cpp.i

.PHONY : src/model.i

# target to preprocess a source file
src/model.cpp.i:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/model.cpp.i
.PHONY : src/model.cpp.i

src/model.s: src/model.cpp.s

.PHONY : src/model.s

# target to generate assembly for a file
src/model.cpp.s:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/model.cpp.s
.PHONY : src/model.cpp.s

src/utils.o: src/utils.cpp.o

.PHONY : src/utils.o

# target to build an object file
src/utils.cpp.o:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/utils.cpp.o
.PHONY : src/utils.cpp.o

src/utils.i: src/utils.cpp.i

.PHONY : src/utils.i

# target to preprocess a source file
src/utils.cpp.i:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/utils.cpp.i
.PHONY : src/utils.cpp.i

src/utils.s: src/utils.cpp.s

.PHONY : src/utils.s

# target to generate assembly for a file
src/utils.cpp.s:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/utils.cpp.s
.PHONY : src/utils.cpp.s

src/validation.o: src/validation.cpp.o

.PHONY : src/validation.o

# target to build an object file
src/validation.cpp.o:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/validation.cpp.o
.PHONY : src/validation.cpp.o

src/validation.i: src/validation.cpp.i

.PHONY : src/validation.i

# target to preprocess a source file
src/validation.cpp.i:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/validation.cpp.i
.PHONY : src/validation.cpp.i

src/validation.s: src/validation.cpp.s

.PHONY : src/validation.s

# target to generate assembly for a file
src/validation.cpp.s:
	$(MAKE) -f CMakeFiles/thesis.dir/build.make CMakeFiles/thesis.dir/src/validation.cpp.s
.PHONY : src/validation.cpp.s

# Help Target
help:
	@echo "The following are some of the valid targets for this Makefile:"
	@echo "... all (the default if no target is provided)"
	@echo "... clean"
	@echo "... depend"
	@echo "... edit_cache"
	@echo "... thesis"
	@echo "... rebuild_cache"
	@echo "... main.o"
	@echo "... main.i"
	@echo "... main.s"
	@echo "... src/FPSCounter.o"
	@echo "... src/FPSCounter.i"
	@echo "... src/FPSCounter.s"
	@echo "... src/Vertex.o"
	@echo "... src/Vertex.i"
	@echo "... src/Vertex.s"
	@echo "... src/model.o"
	@echo "... src/model.i"
	@echo "... src/model.s"
	@echo "... src/utils.o"
	@echo "... src/utils.i"
	@echo "... src/utils.s"
	@echo "... src/validation.o"
	@echo "... src/validation.i"
	@echo "... src/validation.s"
.PHONY : help



#=============================================================================
# Special targets to cleanup operation of make.

# Special rule to run CMake to check the build system integrity.
# No rule that depends on this can have commands that come from listfiles
# because they might be regenerated.
cmake_check_build_system:
	$(CMAKE_COMMAND) -H$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR) --check-build-system CMakeFiles/Makefile.cmake 0
.PHONY : cmake_check_build_system

