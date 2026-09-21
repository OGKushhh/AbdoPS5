#include "kernel/storageScheduler.h"

#include "common/assert.h"
#include "common/logging/log.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace Libs::LibKernel::FileSystem {

StorageScheduler::StorageScheduler() = default;

StorageScheduler::~StorageScheduler() {
	m_running.store(false);
	m_cv.notify_all();
	if (m_worker.joinable()) {
		m_worker.join();
	}
}

void StorageScheduler::Configure(uint32_t bandwidth_mbps) {
	m_bandwidth_mbps = NormalizeBandwidth(bandwidth_mbps);

	if (m_bandwidth_mbps != 0 && !m_running.load()) {
		m_running.store(true);
		m_worker = std::thread(&StorageScheduler::WorkerThread, this);
	}

	if (m_bandwidth_mbps == 0) {
		LOGF("StorageScheduler: disabled (native host speed)\n");
	} else {
		LOGF("StorageScheduler: configured at %u MB/s\n", m_bandwidth_mbps);
	}
}

std::chrono::nanoseconds StorageScheduler::ModelReadTime(uint64_t bytes, bool sequential) const {
	if (m_bandwidth_mbps == 0 || bytes == 0) {
		return std::chrono::nanoseconds::zero();
	}

	const uint64_t bytes_per_second =
	    static_cast<uint64_t>(m_bandwidth_mbps) * 1024ULL * 1024ULL;
	const uint64_t whole_seconds = bytes / bytes_per_second;
	const uint64_t remainder = bytes % bytes_per_second;

	// PS5 SSD: no seek/rotation overhead (unlike PS4 HDD)
	// Just pure transfer time, stretched by the flip ratio
	auto transfer_time = std::chrono::seconds(whole_seconds) +
			     std::chrono::nanoseconds(remainder * 1000000000ULL / bytes_per_second);

	// Apply flip stretch (when emulator runs slower than game's target cadence)
	return std::chrono::nanoseconds(
	    static_cast<int64_t>(transfer_time.count() *
				 static_cast<double>(m_flip_stretch.count())));
}

StorageRequestHandle StorageScheduler::SubmitRead(StorageReadSpans spans, uint64_t offset,
						   int32_t priority,
						   StorageCompletion completion) {
	if (!IsEnabled()) {
		// No throttling — call completion immediately
		if (completion) {
			uint64_t total = 0;
			for (const auto& span : spans) {
				total += span.size;
			}
			completion(static_cast<int64_t>(total), false);
		}
		return nullptr;
	}

	ReadRequest req;
	req.spans = std::move(spans);
	req.offset = offset;
	req.priority = priority;
	req.completion = std::move(completion);
	req.handle = std::make_shared<StorageRequest>();
	req.bytes_total = 0;
	for (const auto& span : req.spans) {
		req.bytes_total += span.size;
	}
	req.sequential = (req.spans.size() <= 1); // Simplified: single-span = sequential

	{
		std::scoped_lock lk(m_mutex);
		m_queue.push(std::move(req));
		m_stats.max_queue_depth = std::max(m_stats.max_queue_depth,
						    static_cast<uint64_t>(m_queue.size()));
	}
	m_cv.notify_one();

	return m_queue.top().handle; // Return the handle of the highest-priority request
}

int64_t StorageScheduler::ReadBlocking(const StorageReadSpans& spans, uint64_t offset,
					int32_t priority) {
	if (!IsEnabled()) {
		// No throttling — read immediately
		uint64_t total = 0;
		for (const auto& span : spans) {
			total += span.size;
		}
		m_stats.bytes_read += total;
		m_stats.chunks++;
		return static_cast<int64_t>(total);
	}

	uint64_t total_bytes = 0;
	bool sequential = (spans.size() <= 1);

	for (const auto& span : spans) {
		total_bytes += span.size;
	}

	// Model the time this read would take on a real PS5 SSD
	auto modeled_time = ModelReadTime(total_bytes, sequential);
	if (modeled_time.count() > 0) {
		auto start = std::chrono::steady_clock::now();
		std::this_thread::sleep_for(modeled_time);
		auto actual = std::chrono::steady_clock::now() - start;
		auto oversleep = std::chrono::duration_cast<std::chrono::nanoseconds>(actual) - modeled_time;
		m_stats.timer_oversleep_ns += oversleep.count() > 0 ?
		    static_cast<uint64_t>(oversleep.count()) : 0;
	}

	m_stats.bytes_read += total_bytes;
	m_stats.chunks++;
	if (sequential) {
		m_stats.sequential_chunks++;
	} else {
		m_stats.positioned_chunks++;
	}
	m_stats.modeled_wait_ns += static_cast<uint64_t>(modeled_time.count());

	return static_cast<int64_t>(total_bytes);
}

void StorageScheduler::WorkerThread() {
	while (m_running.load()) {
		ReadRequest req;

		{
			std::unique_lock lk(m_mutex);
			m_cv.wait(lk, [this] { return !m_queue.empty() || !m_running.load(); });

			if (!m_running.load()) {
				return;
			}

			req = std::move(const_cast<ReadRequest&>(m_queue.top()));
			m_queue.pop();
		}

		if (req.handle && req.handle->IsCanceled()) {
			if (req.completion) {
				req.completion(-1, true);
			}
			continue;
		}

		// Model the read time
		auto modeled_time = ModelReadTime(req.bytes_total, req.sequential);
		if (modeled_time.count() > 0) {
			std::this_thread::sleep_for(modeled_time);
		}

		m_stats.bytes_read += req.bytes_total;
		m_stats.chunks++;
		if (req.sequential) {
			m_stats.sequential_chunks++;
		} else {
			m_stats.positioned_chunks++;
		}
		m_stats.modeled_wait_ns += static_cast<uint64_t>(modeled_time.count());

		if (req.completion) {
			req.completion(static_cast<int64_t>(req.bytes_total), false);
		}
	}
}

StorageSchedulerStats StorageScheduler::GetStats() const {
	return m_stats;
}

void StorageScheduler::ReportGuestFlip(std::chrono::nanoseconds expected_flip_period) {
	// If the emulator is running slower than the game's target frame rate,
	// stretch the modeled I/O time proportionally so the guest perceives
	// the same asset delivery rate as on a real PS5.
	// This is a simplified version — the full implementation would track
	// actual flip intervals and compute a running ratio.
	(void)expected_flip_period; // Not yet implemented — placeholder for future
}

// Global accessor
StorageScheduler& GetStorageScheduler() {
	static StorageScheduler instance;
	return instance;
}

} // namespace Libs::LibKernel::FileSystem
