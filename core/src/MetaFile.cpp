#include "dlm/MetaFile.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dlm {

std::string MetaFile::pathFor(const std::string& outPath) {
    return outPath + ".dlm";
}

bool MetaFile::save(const std::string& metaPath, const DownloadMeta& meta) {
    const std::string tmpPath = metaPath + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }

        out << "url=" << meta.url << "\n";
        out << "totalSize=" << meta.totalSize << "\n";
        out << "chunkSize=" << meta.chunkSize << "\n";
        out << "etag=" << meta.etag << "\n";
        out << "lastModified=" << meta.lastModified << "\n";
        out << "chunks=";

        for (bool done : meta.chunkDone) {
            out << (done ? '1' : '0');
        }
        out << '\n';

        out.flush();
        if (!out) {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, metaPath, ec);
    return !ec;
}

namespace {
bool readline(std::istream& in, const std::string& prefix, std::string& value) {
    std::string line;
    if (!std::getline(in, line)) {
        return false;
    }
    if (line.rfind(prefix, 0) != 0) {
        return false;
    }
    value = line.substr(prefix.size());
    return true;
}
} // namespace

bool MetaFile::load(const std::string& metaPath, DownloadMeta& meta) {
    std::ifstream in(metaPath, std::ios::binary);
    if (!in) {
        return false;
    }

    std::string totalSizeStr;
    std::string chunkSizeStr;
    std::string chunksStr;

    if (!readline(in, "url=", meta.url)) return false;
    if (!readline(in, "totalSize=", totalSizeStr)) return false;
    if (!readline(in, "chunkSize=", chunkSizeStr)) return false;
    if (!readline(in, "etag=", meta.etag)) return false;
    if (!readline(in, "lastModified=", meta.lastModified)) return false;
    if (!readline(in, "chunks=", chunksStr)) return false;

    try {
        meta.totalSize = std::stoll(totalSizeStr);
        meta.chunkSize = std::stoll(chunkSizeStr);
    } catch (const std::exception&) {
        return false;
    }

    meta.chunkDone.assign(chunksStr.size(), false);
    for (std::size_t i = 0; i < chunksStr.size(); ++i) {
        meta.chunkDone[i] = (chunksStr[i] == '1');
    }

    return true;
}

void MetaFile::remove(const std::string& metaPath) {
    std::error_code ec;
    std::filesystem::remove(metaPath, ec);
}

} // namespace dlm
