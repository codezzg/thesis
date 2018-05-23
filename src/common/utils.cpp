#include "utils.hpp"
#include <algorithm>
#include <fstream>
#include <cstring>
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

	debug("loaded file ", path, ": ", dataLen, " bytes (", dataLen / 1024 / 1024., " MiB) into memory");

	return dataLen;
}

void dumpBytes(const void *buffer, std::size_t count, std::size_t maxCount, LogLevel lv) {
	for (unsigned i = 0; i < std::min(count, maxCount); ++i) {
		char str[5];
		snprintf(str, 5, "0x%.2X", *(reinterpret_cast<const uint8_t*>(buffer) + i));
		log(lv, false, str, " ");
	}
	log(lv, true, "");
}
