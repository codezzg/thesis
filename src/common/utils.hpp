#pragma once

#include "logging.hpp"
#include <sstream>
#include <string>
#include <vector>

inline bool startsWith(const std::string& haystack, const std::string& needle)
{
	return haystack.substr(0, needle.size()) == needle;
}

/** Reads a file into memory. The memory is allocated as a vector<char> and returned. */
std::vector<char> readFile(const char* path);

/** Reads a file into a provided buffer.
 *  The load will fail if given buffer length is less than required.
 *  @return -1 in case of failure, else the number of bytes loaded.
 */
std::size_t readFileIntoMemory(const char* path, void* buffer, std::size_t bufsize);

void dumpBytes(const void* buffer, std::size_t count, std::size_t maxCount = 50, LogLevel lv = LOGLV_VERBOSE);
void dumpBytesIntoFile(const char* fname,
	const char* bufname,
	const void* buffer,
	std::size_t bufsize,
	bool append = false);
void dumpBytesIntoFileBin(const char* fname, const void* buffer, std::size_t bufsize, bool append = false);

template <typename T>
std::string listToString(const T& list)
{
	std::stringstream ss;
	ss << "{\n";
	for (unsigned i = 0; i < list.size(); ++i)
		ss << "\t" << list[i] << ",\n";
	ss << "}\n";
	return ss.str();
}

template <typename T, typename F>
std::string mapToString(const T& map, F&& toString = [](auto x) { return x; })
{
	std::stringstream ss;
	ss << "{\n";
	for (const auto& pair : map)
		ss << "\t" << pair.first << " => " << toString(pair.second) << ",\n";
	ss << "}\n";
	return ss.str();
}
