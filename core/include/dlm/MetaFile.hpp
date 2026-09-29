#pragma once

#include <string>

#include "dlm/Types.hpp"

namespace dlm {

	class MetaFile {
	public :
		static std::string pathFor(const std::string& outPath);

		static bool save(const std::string& metaPath, const DownloadMeta& meta);

		static bool load(const std::string& metaPath, DownloadMeta& outMeta);

		static void remove(const  std::string& metaPath);
	};
}