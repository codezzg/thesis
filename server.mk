INC = -Isrc -Isrc/third_party -I/usr/local/src/VulkanSDK/1.0.65.0/x86_64/include

server: server.o src/model.o src/Vertex.o src/endpoint.o src/server_endpoint.o
	g++ $(INC) -o $@ $^ -lpthread

server.o: server.cpp
	g++ $(INC) -c server.cpp -o $@

%.o: %.cpp %.hpp
	g++ -c $(INC) $< -o $@

.PHONY: clean
clean:
	rm src/*.o server -f
