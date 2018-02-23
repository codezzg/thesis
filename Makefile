VULKAN_SDK_PATH = /usr/local/src/VulkanSDK/1.0.65.0/x86_64

CFLAGS = -std=c++11 -I$(VULKAN_SDK_PATH)/include -ggdb -Wall -Wextra -pedantic
LDFLAGS = -L$(VULKAN_SDK_PATH)/lib `pkg-config --static --libs glfw3` -lvulkan

VulkanTest: main.cpp
	g++ $(CFLAGS) -o VulkanTest main.cpp $(LDFLAGS)

.PHONY: test clean

test: VulkanTest
	./VulkanTest

clean:
	rm -f VulkanTest
