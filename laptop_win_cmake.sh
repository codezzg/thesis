#!/bin/bash

export VULKAN_SDK=/c/VulkanSDK/1.1.73.0
export GLM_ROOT_DIR=/c/Users/jack/glm-0.9.8.5/glm
export GLFW_LOCATION=/c/Users/jack/glfw-3.2.1.bin.WIN64/glfw-3.2.1.bin.WIN64

rm -f CMakeCache.txt
rm -rf CMakeFiles/

cmake -G "Visual Studio 15 2017 Win64"
