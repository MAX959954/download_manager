#pragma once

#include <atomic>

namespace dlm {

	class CancelToken {
	public :
		void pause() { paused_.store(true); }
		void resume() { paused_.store(false); }
		void cancel() { cancelled_.store(true); }

		bool isPaused() const { return paused_.load(); }
		bool isCancelled() const { return cancelled_.load(); }

		bool shouldAbortTransfer() const { return paused_.load() || cancelled_.load(); }
	private :
		std::atomic <bool> paused_{ false };
		std::atomic <bool>	cancelled_{ false };
	};
}