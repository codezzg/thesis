#pragma once

#include <cstddef>

constexpr std::size_t kilobytes(float n)
{
	return static_cast<std::size_t>(n * 1024);
}

constexpr std::size_t megabytes(float n)
{
	return static_cast<std::size_t>(n * 1024 * 1024);
}
