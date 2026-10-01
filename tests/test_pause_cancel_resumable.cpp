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

	// writeChunkSize маленький + искусственная задержка между кусочками —
	// иначе FakeHttpClient отдаёт данные мгновенно и вся закачка успевает
	// завершиться раньше, чем сработает pause() из соседнего потока.
	dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/50,
	                                     /*etag=*/"", /*failIfRangeMismatch=*/false,
	                                     /*delayPerPiece=*/std::chrono::milliseconds(2));
	dlm::Downloader downloader(fakeClient);
	dlm::CancelToken token;

	// Через небольшую задержку ставим загрузку на паузу прямо посреди неё.
	std::thread pauser([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		token.pause();
	});

	const dlm::DownloadResult first =
		downloader.downloadResumable(url, outputPath, chunkSize, 4, &token);
	pauser.join();

	// Загрузка должна была прерваться, не завершившись успехом, но метафайл
	// должен остаться — в нём отмечено, что реально успели докачать.
	assert(!first.success);
	assert(fileExists(metaPath));

	// "Возобновляем" загрузку — снимаем паузу и качаем уже без токена.
	token.resume();
	const dlm::DownloadResult second = downloader.downloadResumable(url, outputPath, chunkSize, 4);

	assert(second.success);
	assert(readline(outputPath) == body);
	assert(!fileExists(metaPath)); // метафайл убран после успешного завершения

	std::remove(outputPath.c_str());

	std::printf("test_pause_cancel_resumable: OK\n");
	return 0;
}
