#include "server_resources.hpp"

ServerResources::ServerResources(uint8_t* memory, std::size_t memsize)
	: memory{ memory }
	, memsize{ memsize }
	, allocator{ memory, memsize }
{}

Model ServerResources::loadModel(const char* file)
{
	const auto fileSid = sid(file);
	if (models.count(fileSid) > 0) {
		logging::warn("Tried to load model ", file, " which is already loaded!");
		return models[fileSid];
	}

	// Reserve the whole remaining memory for loading the resource, then shrink to fit.
	auto buffer = allocator.allocAll();

	auto& model = models[fileSid];
	model = ::loadModel(file, buffer);

	allocator.deallocLatest();
	allocator.alloc(model.size());

	return model;
}

shared::Texture ServerResources::loadTexture(const char* file)
{
	const auto fileSid = sid(file);
	if (textures.count(fileSid) > 0) {
		logging::warn("Tried to load texture ", file, " which is already loaded!");
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

	logging::info("Loaded texture ", file, " (", texture.size / 1024., " KiB)");

	return texture;
}
