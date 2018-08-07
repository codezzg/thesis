#include "server_resources.hpp"

using namespace logging;

ServerResources::~ServerResources()
{
	for (auto cd : modelsColdData)
		delete cd;
}

Model ServerResources::loadModel(const char* file)
{
	const auto fileSid = sid(file);
	Model model;
	if (models.lookup(fileSid, fileSid, model)) {
		warn("Tried to load model ", file, " which is already loaded!");
		return model;
	}

	// Reserve the whole remaining memory for loading the resource, then shrink to fit.
	std::size_t bufsize;
	auto buffer = allocator.allocAll(&bufsize);

	// Model cold data is stored in a separate chunk of memory
	auto coldData = new ModelColdData;
	model = ::loadModel(file, buffer, coldData, bufsize);
	assert(model.vertices && "Failed to load model!");

	models.set(fileSid, fileSid, model);
	modelsColdData.emplace_back(coldData);

	allocator.deallocLatest();
	allocator.alloc(model.size());

	return model;
}

shared::Texture ServerResources::loadTexture(const char* file)
{
	const auto fileSid = sid(file);
	if (textures.count(fileSid) > 0) {
		warn("Tried to load texture ", file, " which is already loaded!");
		return textures[fileSid];
	}

	std::size_t bufsize;
	auto buffer = allocator.allocAll(&bufsize);
	auto size = readFileIntoMemory(file, buffer, bufsize);

	assert(size > 0 && "Failed to load texture!");

	auto& texture = textures[fileSid];
	texture.size = size;
	texture.data = buffer;
	texture.format = shared::TextureFormat::UNKNOWN;

	allocator.deallocLatest();
	allocator.alloc(texture.size);

	info("Loaded texture ", file, " (", texture.size / 1024., " KiB)");

	return texture;
}

shared::SpirvShader ServerResources::loadShader(const char* file)
{
	const auto fileSid = sid(file);
	if (shaders.count(fileSid) > 0) {
		warn("Tried to load shader ", file, " which is already loaded!");
		return shaders[fileSid];
	}

	std::size_t bufsize;
	auto buffer = allocator.allocAll(&bufsize);
	auto size = readFileIntoMemory(file, buffer, bufsize);

	assert(size > 0 && "Failed to load shader!");

	auto& shader = shaders[fileSid];
	shader.codeSizeInBytes = size;
	shader.code = reinterpret_cast<uint32_t*>(buffer);

	allocator.deallocLatest();
	allocator.alloc(shader.codeSizeInBytes);

	info("Loaded shader ", file, " (", shader.codeSizeInBytes, " B)");

	return shader;
}

void ServerResources::onInit()
{
	// Reserve initial memory to the models hashmap
	const auto modelsMemsize = CF_HASHMAP_GET_BUFFER_SIZE(StringId, Model, 128);
	if (memsize < modelsMemsize) {
		err("ServerResources needs more than ",
			modelsMemsize,
			" B to work! Reserve more memory for ServerResources!");
		throw;
	}
	models = cf::hashmap<StringId, Model>::create(modelsMemsize, memory);
	allocator.init(memory + modelsMemsize, memsize - modelsMemsize);
}
