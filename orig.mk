VULKAN_SDK_PATH = /usr/local/src/VulkanSDK/1.0.65.0/x86_64
CFLAGS = -std=c++14 -I$(VULKAN_SDK_PATH)/include -Isrc/third_party -ggdb
LDFLAGS = -L$(VULKAN_SDK_PATH)/lib `pkg-config --static --libs glfw3` -lvulkan

VulkanTest: main_orig.cpp
	g++ $(CFLAGS) -o VulkanTest $< $(LDFLAGS)
