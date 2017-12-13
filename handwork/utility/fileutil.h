// Provide common file operation.

#pragma once

#include "../handwork.h"
#include <string>
#include <cctype>
#include <string.h>

namespace handwork
{
	// Platform independent filename-handling functions.
	bool IsAbsolutePath(const std::string &filename);
	std::string AbsolutePath(const std::string &filename);
	std::string ResolveFilename(const std::string &filename);
	std::string DirectoryContaining(const std::string &filename);
	void SetSearchDirectory(const std::string &dirname);

	inline bool HasExtension(const std::string &value, const std::string &ending) {
		if (ending.size() > value.size()) return false;
		return std::equal(
			ending.rbegin(), ending.rend(), value.rbegin(),
			[](char a, char b) { return std::tolower(a) == std::tolower(b); });
	}

}	// namespace handwork