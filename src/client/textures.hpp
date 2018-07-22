#pragma once

#include "images.hpp"
#include "shared_resources.hpp"
#include "third_party/stb_image.h"
#include <future>
#include <mutex>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

struct Application;
struct Buffer;

enum CubeFaceIndex : uint32_t {
	CUBE_FACE_POS_X = 0,
	CUBE_FACE_NEG_X = 1,
	CUBE_FACE_POS_Y = 2,
	CUBE_FACE_NEG_Y = 3,
	CUBE_FACE_POS_Z = 4,
	CUBE_FACE_NEG_Z = 5
};

// TODO
class TextureLoader final {
	struct ImageInfo {
		VkFormat format;
		uint32_t width;
		uint32_t height;
	};

	Buffer& stagingBuffer;
	std::size_t stagingBufferOffset = 0;

	/** Used by `addTextureAsync` */
	mutable std::mutex mtx;

	std::vector<ImageInfo> imageInfos;
	std::vector<Image*> images;

	/** Holds the latest error message */
	std::string latestError = "";

	void saveImageInfo(Image& image,
		stbi_uc* pixels,
		int texWidth,
		int texHeight,
		int texChannels,
		shared::TextureFormat format);

public:
	explicit TextureLoader(Buffer& stagingBuffer)
		: stagingBuffer{ stagingBuffer }
	{}

	/** Load a texture from raw data pointed by `texture` */
	bool addTexture(Image& image, const shared::Texture& texture);
	/** Load a texture from file with given format.  */
	bool addTexture(Image& image, const std::string& texturePath, shared::TextureFormat format);

	/** Like `addTexture`, but asynchronous. Returns a `future` which must be waited for
	 *  before calling `create`. It is the caller's responsibility to wait it.
	 *  @return a Future which will contain `true` in case of success, `false` otherwise.
	 */
	std::future<bool> addTextureAsync(Image& image, const shared::Texture& texture);
	/** @see addTextureAsync */
	std::future<bool> addTextureAsync(Image& image, const std::string& texturePath, shared::TextureFormat format);

	void create(const Application& app);

	std::string getLatestError() const
	{
		std::lock_guard<std::mutex> lock{ mtx };
		return latestError;
	}
};

/** Create a sampler appropriate for sampling a texture and return it. */
VkSampler createTextureSampler(const Application& app);

/** Create a sampler appropriate for sampling a textureCube and return it. */
VkSampler createTextureCubeSampler(const Application& app);

/** Creates a texture cubemap along with its backing image.
 *  In case of failure, returned Image will have its handle set to VK_NULL_HANDLE.
 */
Image createTextureCube(const Application& app, const std::array<std::string, 6>& faces);
