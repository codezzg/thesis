CFLAGS = -std=c++14 -Wall -pedantic -Wextra -ggdb 
INC = -Isrc -Isrc/third_party -I/c/VulkanSDK/1.1.70.1/Include -I/home/jacktommy/glm-0.9.9-a2
LIB = -lws2_32

server: server.o src/model.o src/vertex.o src/endpoint.o src/server_endpoint.o src/camera.o src/serialization.o src/server_appstage.o src/endpoint_xplatform.o
	g++ $(CFLAGS) $(INC) -o $@ $^ -lpthread $(LIB)

server.o: server.cpp
	g++ $(CFLAGS) $(INC) -c server.cpp -o $@

%.o: %.cpp %.hpp src/config.hpp
	g++ -c $(CFLAGS) $(INC) $< -o $@

.PHONY: clean
clean:
	rm src/*.o server -f
