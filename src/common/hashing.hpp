#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#ifndef NDEBUG
#	include <unordered_map>
#endif

using StringId = uint32_t;

constexpr StringId SID_NONE = 0;

namespace hashing {

#ifdef _WIN32
inline uint32_t fnv1a_hash(const char* buffer)
{
#else
constexpr uint32_t fnv1a_hash(const char* buffer)
{
#endif
	constexpr uint32_t fnv_prime32 = 16777619;
	uint32_t result = 2166136261;
	int i = 0;
	while (buffer[i] != '\0') {
		result ^= static_cast<uint32_t>(buffer[i++]);
		result *= fnv_prime32;
	}
	assert(result != SID_NONE);
	return result;
}

#ifdef _WIN32
inline uint32_t fnv1a_hash(const uint8_t* buffer, std::size_t bufsize)
{
#else
constexpr uint32_t fnv1a_hash(const uint8_t* buffer, std::size_t bufsize)
{
#endif
	constexpr uint32_t fnv_prime32 = 16777619;
	uint32_t result = 2166136261;
	unsigned i = 0;
	while (i < bufsize) {
		result ^= static_cast<uint32_t>(buffer[i++]);
		result *= fnv_prime32;
	}
	assert(result != SID_NONE);
	return result;
}

}   // end namespace hashing

#ifndef NDEBUG

class StringIdMap : public std::unordered_map<StringId, std::string> {
public:
	void addUnique(StringId key, const std::string& value)
	{
		auto it = find(key);
		if (it == end()) {
			emplace(key, value);
			return;
		}

		// If key is already in the map, the value must be the same
		if (it->second != value) {
			throw std::invalid_argument("Two strings match the same StringId: '" + it->second + "'" +
						    " and '" + value + "' !!!");
		}
	}
};

// Maps StringId => original string
extern StringIdMap stringDb;

#endif

#ifdef NDEBUG

#	ifndef _WIN32
constexpr StringId sid(const char* buf)
#	else
inline StringId sid(const char* buf)
#	endif
{
	return hashing::fnv1a_hash(buf);
}

inline StringId sid(const std::string& str)
{
	return hashing::fnv1a_hash(str.c_str());
}

inline std::string sidToString(StringId id)
{
	return std::to_string(static_cast<uint32_t>(id));
}
#else
inline StringId sid(const char* buf)
{
	const auto h = hashing::fnv1a_hash(buf);
	stringDb.addUnique(h, std::string{ buf });
	return h;
}

inline StringId sid(const std::string& str)
{
	const auto h = hashing::fnv1a_hash(str.c_str());
	stringDb.addUnique(h, str);
	return h;
}

inline std::string sidToString(StringId id)
{
	return stringDb[id];
}
#endif
