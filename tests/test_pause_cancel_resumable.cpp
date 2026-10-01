#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <dlm/Downloader.hpp>
#include <dlm/CancelToken.hpp>
#include <dlm/MetaFile.hpp>

#include "FakeHttpClient.hpp"

namespace {

	std::string readline(const std::string& path) {
		std::ifstream in(path, std::ios::binary);
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	bool fileExists(const std::string& path) {
		std::ifstream in(path);
		return static_cast<bool>(in);
	}

	std::string makeBody(std::size_t size) {
		std::string body(size, '\n');
		for (std::size_t i = 0; i < size; i++) {
			body[i] = static_cast<char>('A' + (i % 26));
		}
		return body;
	}
}

int main() {
	const std::string body = makeBody(50 * 1024);
	const std::int64_t chunkSize = 2000;
	const std::string url = "http://example.invalid/file";
	const std::string outputPath = "test_pause_cancel.tmp";
	const std::string metaPath = dlm::MetaFile::pathFor(outputPath);

	// writeChunkSize is small + there's an artificial delay between pieces —
	// otherwise FakeHttpClient delivers data instantly and the whole download
	// manages to finish before pause() from the other thread kicks in.
	dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/50,
	                                     /*etag=*/"", /*failIfRangeMismatch=*/false,
	                                     /*delayPerPiece=*/std::chrono::milliseconds(2));
	dlm::Downloader downloader(fakeClient);
	dlm::CancelToken token;

	// After a short delay, pause the download right in the middle of it.
	std::thread pauser([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		token.pause();
	});

	const dlm::DownloadResult first =
		downloader.downloadResumable(url, outputPath, chunkSize, 4, &token);
	pauser.join();

	// The download should have been interrupted without succeeding, but the
	// meta file should remain — it records what was actually downloaded so far.
	assert(!first.success);
	assert(fileExists(metaPath));

	// "Resume" the download — lift the pause and download without the token this time.
	token.resume();
	const dlm::DownloadResult second = downloader.downloadResumable(url, outputPath, chunkSize, 4);

	assert(second.success);
	assert(readline(outputPath) == body);
	assert(!fileExists(metaPath)); // the meta file is removed after successful completion

	std::remove(outputPath.c_str());

	std::printf("test_pause_cancel_resumable: OK\n");
	return 0;
}
