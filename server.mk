INC = -Isrc -Isrc/third_party

server: server.o src/model.o src/Vertex.o src/endpoint.o src/server_endpoint.o
	g++ $(INC) -o $@ $^ -lpthread

server.o: server.cpp
	g++ $(INC) -c server.cpp -o $@

%.o: %.cpp %.hpp
	g++ -c $(INC) $< -o $@

.PHONY: clean
clean:
	rm src/*.o server -f
