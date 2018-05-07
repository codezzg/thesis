#pragma once

#include <cstdint>
#include <string>
#ifndef NDEBUG
	#include <unordered_map>
#endif

namespace hashing {

constexpr uint32_t fnv1_hash(const char* buffer) {
	constexpr uint32_t fnv_prime32 = 16777619;
	uint32_t result = 2166136261;
	int i = 0;
	while (buffer[i] != '\0') {
		result *= fnv_prime32;
		result ^= static_cast<uint32_t>(buffer[i++]);
	}
	return result;
}

}

using StringId = uint32_t;

#ifndef NDEBUG

class StringIdMap : public std::unordered_map<StringId, std::string> {
public:
	void addUnique(StringId key, const std::string& value) {
		auto it = find(key);
		if (it == end()) {
			emplace(key, value);
			return;
		}

		// If key is already in the map, the value must be the same
		if (it->second != value) {
			throw std::invalid_argument("Two strings match the same StringId: '" + it->second
				+ "'" + " and '" + value + "' !!!");
		}
	}
};

// Maps StringId => original string
extern StringIdMap stringDb;

#endif

#ifdef NDEBUG
constexpr StringId sid(const char *buf) {
	return hashing::fnv1_hash(buf);
}

inline StringId sid(const std::string& str) {
	return hashing::fnv1_hash(str.c_str());
}
#else
inline StringId sid(const char *buf) {
	const auto h = hashing::fnv1_hash(buf);
	stringDb.addUnique(h, std::string{ buf });
	return h;
}

inline StringId sid(const std::string& str) {
	const auto h = hashing::fnv1_hash(str.c_str());
	stringDb.addUnique(h, str);
	return h;
}
#endif
