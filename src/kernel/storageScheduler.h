#ifndef KYTY_KERNEL_STORAGE_SCHEDULER_H_
#define KYTY_KERNEL_STORAGE_SCHEDULER_H_

#include "common/common.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <thread>

namespace Libs::LibKernel::FileSystem {

// Kyty-009: PS5 Storage I/O Scheduler.
//
// The PS5's custom SSD has specific bandwidth characteristics (~5.5 GB/s
// raw, ~8-9 GB/s compressed). Some PS5 games time their asset streaming
// to the SSD's specific latency. On a fast host NVMe, the streaming logic
// may break (texture pop-in, anti-cheat false positives, audio glitches).
//
// This scheduler throttles file reads to emulate the PS5 SSD's bandwidth.
// Unlike the PS4 version (which models HDD seek+rotation), the PS5 version
// is pure bandwidth throttling — no seek/rotation overhead.
//
// Bandwidth profiles:
//   0:     Native (no throttling — use host SSD speed)
//   5500:  PS5 SSD raw read speed
//   8800:  PS5 SSD compressed read speed
//   14000: PS5 Pro SSD (rumored faster)
//
// Based on the shadPS4 Shadlix fork's storage_scheduler, adapted for PS5.

constexpr uint32_t PS5_SSD_BANDWIDTH_RAW       = 5500;  // MB/s
constexpr uint32_t PS5_SSD_BANDWIDTH_COMPRESSED = 8800;  // MB/s
constexpr uint32_t PS5_PRO_SSD_BANDWIDTH        = 14000; // MB/s (rumored)
constexpr uint32_t STORAGE_BANDWIDTH_NATIVE     = 0;     // No throttling

[[nodiscard]] inline bool IsSupportedBandwidth(uint32_t bandwidth_mbps) {
	switch (bandwidth_mbps) {
		case 0:
		case 5500:
		case 8800:
		case 14000:
			return true;
		default:
			return false;
	}
}

// Clamp unsupported values to the nearest supported profile
[[nodiscard]] inline uint32_t NormalizeBandwidth(uint32_t bandwidth_mbps) {
	if (IsSupportedBandwidth(bandwidth_mbps)) {
		return bandwidth_mbps;
	}
	// Clamp to nearest PS5 profile
	if (bandwidth_mbps < 2750) return 0;       // Too slow — let host handle it
	if (bandwidth_mbps < 7150) return 5500;      // Near raw speed
	if (bandwidth_mbps < 11400) return 8800;     // Near compressed speed
	return 14000;                                 // Fast — PS5 Pro
}

struct StorageReadSpan {
	void* data  = nullptr;
	uint64_t size = 0;
};

using StorageReadSpans = std::vector<StorageReadSpan>;

class StorageRequest {
public:
	[[nodiscard]] bool IsCanceled() const { return m_canceled.load(); }
	void Cancel() { m_canceled.store(true); }

private:
	friend class StorageScheduler;
	std::atomic_bool m_canceled{};
};

using StorageRequestHandle = std::shared_ptr<StorageRequest>;
using StorageCompletion = std::function<void(int64_t result, bool canceled)>;

struct StorageSchedulerStats {
	uint64_t bytes_read         = 0;
	uint64_t chunks              = 0;
	uint64_t sequential_chunks   = 0;
	uint64_t positioned_chunks  = 0;
	uint64_t modeled_wait_ns    = 0;
	uint64_t timer_oversleep_ns = 0;
	uint64_t host_overrun_ns    = 0;
	uint64_t host_wait_ns       = 0;
	uint64_t prefetched_chunks  = 0;
	uint64_t demand_chunks       = 0;
	uint64_t max_staging_buffers = 0;
	uint64_t max_queue_depth    = 0;
};

class StorageScheduler {
public:
	StorageScheduler();
	~StorageScheduler();

	StorageScheduler(const StorageScheduler&) = delete;
	StorageScheduler& operator=(const StorageScheduler&) = delete;

	// Configure the scheduler with a bandwidth limit in MB/s.
	// 0 = no throttling (native host speed).
	void Configure(uint32_t bandwidth_mbps);

	[[nodiscard]] bool IsEnabled() const { return m_bandwidth_mbps != 0; }
	[[nodiscard]] uint32_t GetBandwidthMbps() const { return m_bandwidth_mbps; }

	// Submit an async read. Returns immediately; the completion callback
	// is called when the read completes (or is canceled).
	StorageRequestHandle SubmitRead(StorageReadSpans spans, uint64_t offset,
					 int32_t priority, StorageCompletion completion);

	// Blocking read. Returns total bytes read, or -1 on error.
	int64_t ReadBlocking(const StorageReadSpans& spans, uint64_t offset, int32_t priority = 0);

	// Cancel a pending read request.
	void Cancel(const StorageRequestHandle& request) {
		if (request) {
			request->Cancel();
		}
	}

	[[nodiscard]] StorageSchedulerStats GetStats() const;

	// Called from the present thread on every guest flip. When the emulator
	// runs slower than the game's target cadence, modeled I/O time is stretched
	// so the guest-perceived delivery rate per frame stays close to real PS5.
	void ReportGuestFlip(std::chrono::nanoseconds expected_flip_period);

private:
	void WorkerThread();
	std::chrono::nanoseconds ModelReadTime(uint64_t bytes, bool sequential) const;

	struct ReadRequest {
		StorageReadSpans spans;
		uint64_t offset;
		int32_t priority;
		StorageCompletion completion;
		StorageRequestHandle handle;
		uint64_t bytes_total;
		bool sequential;
	};

	struct RequestComparator {
		bool operator()(const ReadRequest& a, const ReadRequest& b) const {
			return a.priority < b.priority; // Lower priority value = higher priority
		}
	};

	uint32_t m_bandwidth_mbps = 0;
	std::atomic_bool m_running{false};
	std::thread m_worker;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::priority_queue<ReadRequest, std::vector<ReadRequest>, RequestComparator> m_queue;
	StorageSchedulerStats m_stats{};
	std::chrono::nanoseconds m_flip_stretch{1}; // 1.0 = no stretch
};

// Global accessor
StorageScheduler& GetStorageScheduler();

} // namespace Libs::LibKernel::FileSystem

#endif // KYTY_KERNEL_STORAGE_SCHEDULER_H_
