#include "model.hpp"
#include <sparsehash/dense_hash_map>
#include <chrono>
#include <iostream>
#ifdef USE_EXPERIMENTAL_TINYOBJ
#	define TINYOBJ_LOADER_OPT_IMPLEMENTATION
#	include "third_party/tinyobj_loader_opt.h"
#	include <cstdlib>
#else
#	define TINYOBJLOADER_IMPLEMENTATION
#	include "third_party/tiny_obj_loader.h"
#endif

#ifdef USE_EXPERIMENTAL_TINYOBJ
static const char* mmap_file(size_t *len, const char *filename) {
	*len = 0;
	FILE* f = fopen(filename, "rb" );
	if (!f) {
		fprintf(stderr, "Failed to open file : %s\n", filename);
		return nullptr;
	}
	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fclose(f);

	if (fileSize < 16) {
		fprintf(stderr, "Empty or invalid .obj : %s\n", filename);
		return nullptr;
	}

	struct stat sb;
	char *p;
	int fd;

	fd = open (filename, O_RDONLY);
	if (fd == -1) {
		perror ("open");
		return nullptr;
	}

	if (fstat (fd, &sb) == -1) {
		perror ("fstat");
		return nullptr;
	}

	if (!S_ISREG (sb.st_mode)) {
		fprintf (stderr, "%s is not a file\n", "lineitem.tbl");
		return nullptr;
	}

	p = (char*)mmap (0, fileSize, PROT_READ, MAP_SHARED, fd, 0);

	if (p == MAP_FAILED) {
		perror ("mmap");
		return nullptr;
	}

	if (close (fd) == -1) {
		perror ("close");
		return nullptr;
	}

	(*len) = fileSize;

	return p;
}
#endif // USE_EXPERIMENTAL_TINYOBJ


void loadModel(const char *modelPath, std::vector<Vertex>& vertices, std::vector<Index>& indices) {

#ifdef USE_EXPERIMENTAL_TINYOBJ
	namespace to = tinyobj_opt;
#else
	namespace to = tinyobj;
#endif

	to::attrib_t attrib;
	std::vector<to::shape_t> shapes;
	std::vector<to::material_t> materials;
	std::string err;

	const auto load_t_begin = std::chrono::high_resolution_clock::now();
#ifdef USE_EXPERIMENTAL_TINYOBJ
	size_t data_len = 0;
	const char* data = mmap_file(&data_len, modelPath);
	if (data == nullptr) {
		printf("failed to load file\n");
		exit(-1);
		return ;
	}
	auto load_t_end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> load_ms = load_t_end - load_t_begin;
	std::cout << "filesize: " << data_len << std::endl;

	tinyobj_opt::LoadOption option;
	auto num_threads = 4;
	option.req_num_threads = num_threads;
	option.verbose = true;
	bool ret = to::parseObj(&attrib, &shapes, &materials, data, data_len, option);
	std::cout << "load time: " << load_ms.count() << " [msecs]" << std::endl;
	if (!ret)
		throw std::runtime_error("failed to load model!");
#else
	if (!to::LoadObj(&attrib, &shapes, &materials, &err, modelPath))
		throw std::runtime_error(err);
	auto load_t_end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> load_ms = load_t_end - load_t_begin;
	std::cout << "load time: " << load_ms.count() << " [msecs]" << std::endl;
#endif

	google::dense_hash_map<Vertex, uint32_t> uniqueVertices;
	uniqueVertices.set_empty_key(VERTEX_EMPTY_KEY);

#ifdef USE_EXPERIMENTAL_TINYOBJ
	for (const auto& index : attrib.indices) {
#else
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
#endif
			Vertex vertex = {};
			vertex.pos = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2],
			};
			vertex.texCoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
			};
			vertex.color = {1.0f, 1.0f, 1.0f};

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = vertices.size();
				vertices.emplace_back(vertex);
			}

			indices.emplace_back(uniqueVertices[vertex]);
#ifndef USE_EXPERIMENTAL_TINYOBJ
		}
#endif
	}
	std::cout << "size = " << vertices.size() << ", " << indices.size() << "\n";
}
