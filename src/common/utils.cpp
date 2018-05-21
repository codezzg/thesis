#include "utils.hpp"
#include <fstream>
#include "logging.hpp"

using namespace logging;

std::vector<char> readFileIntoMemory(const char *path) {
	std::ifstream file{ path, std::ios::binary | std::ios::ate };
	auto dataLen  = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<char> data(dataLen);
	if (!file.read(data.data(), dataLen)) {
		err("Failed to load file: ", path);
		// Deallocate vector
		std::vector<char>().swap(data);
	}

	info("loaded file ", path, ": ", data.size(), " bytes (", data.size() / 1024 / 1024., " MiB) into memory");
	return data;
}

std::size_t readFileIntoMemory(const char *path, void *buffer, std::size_t bufsize) {
	std::ifstream file{ path, std::ios::binary | std::ios::ate };
	auto dataLen  = file.tellg();

	if (!file.good()) {
		err("File ", path, " is not good.");
		return -1;
	}

	if (dataLen < 0) {
		err("Failed to tellg(): ", path);
		return -1;
	}

	if (dataLen > static_cast<signed>(bufsize)) {
		err("readFileIntoMemory(", path, "): buffer is too small! (",
				bufsize, " while needing ", dataLen, " bytes.)");
		return -1;
	}

	file.seekg(0, std::ios::beg);

	if (!file.read(reinterpret_cast<char*>(buffer), dataLen)) {
		err("Failed to load file: ", path);
		return -1;
	}

	info("loaded file ", path, ": ", dataLen, " bytes (", dataLen / 1024 / 1024., " MiB) into memory");

	return dataLen;
}
