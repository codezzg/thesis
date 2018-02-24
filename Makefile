CC = g++

VULKAN_SDK_PATH = /usr/local/src/VulkanSDK/1.0.65.0/x86_64

CFLAGS = -std=c++17 -I$(VULKAN_SDK_PATH)/include -ggdb -Wall -Wextra -pedantic
LDFLAGS = -L$(VULKAN_SDK_PATH)/lib `pkg-config --static --libs glfw3` -lvulkan
INCLUDES = -Isrc

VulkanTest: main.o src/Vertex.o src/FPSCounter.o src/validation.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -o VulkanTest $^ $(LDFLAGS)

main.o: main.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

src/%.o: src/%.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: test clean

test: VulkanTest
	./VulkanTest

clean:
	rm -f VulkanTest
