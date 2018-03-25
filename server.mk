INC = -ggdb -Isrc -Isrc/third_party -I/usr/local/src/VulkanSDK/1.0.65.0/x86_64/include

server: server.o src/model.o src/vertex.o src/endpoint.o src/server_endpoint.o src/camera.o src/serialization.o
	g++ $(INC) -o $@ $^ -lpthread

server.o: server.cpp
	g++ $(INC) -c server.cpp -o $@

%.o: %.cpp %.hpp src/config.hpp
	g++ -c $(INC) $< -o $@

.PHONY: clean
clean:
	rm src/*.o server -f
