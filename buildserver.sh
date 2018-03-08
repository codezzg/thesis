#g++ -ggdb -O0 -Isrc server.cpp src/endpoint.cpp src/model.cpp src/Vertex.cpp -o server -pthread
make -f server.mk
