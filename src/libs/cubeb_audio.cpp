// SPDX-FileCopyrightText: Copyright 2026 KytyPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-011: Cubeb audio backend.
//
// Cubeb is Mozilla's cross-platform audio library used by Firefox, Citra,
// RPCS3, and Dolphin. It has meaningfully lower latency than SDL on
// Linux/PipeWire and better device routing on macOS.
//
// This file provides OpenCubebDevice/CloseCubebDevice/CubebQueueAudio
// functions that mirror the existing SDL audio path. The backend is
// selected at runtime via Config::GetAudioBackend().
//
// To enable Cubeb:
// 1. Add cubeb as a submodule: 3rdparty/cubeb
// 2. Add to CMakeLists.txt: add_subdirectory(3rdparty/cubeb)
// 3. Link: target_link_libraries(kyty_emulator PRIVATE cubeb)
// 4. Set config: audio_backend = "cubeb"

#include "common/common.h"
#include "common/logging/log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// When cubeb is available, include it here:
// #include <cubeb/cubeb.h>

namespace Libs::Audio {

// Audio format info (mirrors the Format enum from audio_internal.h)
enum class CubebFormat {
	Signed16bitMono,
	Signed16bitStereo,
	Signed16bit8Ch,
	FloatMono,
	FloatStereo,
	Float8Ch,
};

struct CubebPortState {
	void*       cubeb_stream = nullptr; // cubeb_stream* (cast to void* for ABI compat)
	uint32_t    sample_rate  = 0;
	uint32_t    channels     = 0;
	uint32_t    samples_num  = 0;
	bool        is_float     = false;
	bool        paused       = true;
	// Ring buffer for audio data (since Cubeb uses callbacks, not queue)
	std::vector<uint8_t> ring_buffer;
	uint64_t              ring_write_pos = 0;
	uint64_t              ring_read_pos  = 0;
	uint64_t              ring_capacity  = 0;
};

// Cubeb state (initialized once)
static void* g_cubeb_context = nullptr; // cubeb* (cast to void*)
static bool   g_cubeb_initialized = false;

bool InitCubeb() {
	if (g_cubeb_initialized) {
		return g_cubeb_context != nullptr;
	}
	g_cubeb_initialized = true;

	// When cubeb is linked:
	// int rv = cubeb_init(&context, "KytyPS5", nullptr);
	// if (rv != CUBEB_OK) {
	//     LOGF("Cubeb: cubeb_init failed: %d\n", rv);
	//     return false;
	// }
	// g_cubeb_context = context;

	LOGF("Cubeb: backend not linked (cubeb submodule not initialized). "
	     "Falling back to SDL.\n");
	return false;
}

void ShutdownCubeb() {
	if (g_cubeb_context) {
		// cubeb_destroy(static_cast<cubeb*>(g_cubeb_context));
		g_cubeb_context = nullptr;
	}
	g_cubeb_initialized = false;
}

bool IsCubebAvailable() {
	if (!g_cubeb_initialized) {
		InitCubeb();
	}
	return g_cubeb_context != nullptr;
}

// Cubeb audio callback (called from Cubeb's audio thread)
static long CubebDataCallback(void* user_data, void* output_buffer, long frames) {
	auto* state = static_cast<CubebPortState*>(user_data);
	if (state == nullptr || state->paused || state->ring_buffer.empty()) {
		std::memset(output_buffer, 0, frames * state->channels * (state->is_float ? 4 : 2));
		return frames;
	}

	// Copy from ring buffer to output
	const auto bytes_per_frame = state->channels * (state->is_float ? 4 : 2);
	const auto needed_bytes = static_cast<uint64_t>(frames) * bytes_per_frame;
	const auto available_bytes =
	    (state->ring_write_pos >= state->ring_read_pos)
		? (state->ring_write_pos - state->ring_read_pos)
		: (state->ring_capacity - state->ring_read_pos + state->ring_write_pos);

	if (available_bytes >= needed_bytes) {
		// Enough data — copy from ring
		// (Simplified — real implementation handles wrap-around)
		std::memcpy(output_buffer,
			    state->ring_buffer.data() + (state->ring_read_pos % state->ring_capacity),
			    needed_bytes);
		state->ring_read_pos += needed_bytes;
	} else {
		// Not enough data — output silence for the remaining frames
		std::memset(output_buffer, 0, needed_bytes);
	}

	return frames;
}

static void CubebStateCallback(void* user_data, int state) {
	// Handle device state changes (e.g., device unplugged)
	LOGF("Cubeb: state changed: %d\n", state);
}

bool OpenCubebDevice(uint32_t freq, uint32_t channels, CubebFormat format,
		     uint32_t samples_num, CubebPortState* out_state) {
	if (!IsCubebAvailable()) {
		return false;
	}

	if (g_cubeb_context == nullptr) {
		return false;
	}

	// When cubeb is linked:
	// cubeb_stream_params params;
	// params.format = (format is float) ? CUBEB_SAMPLE_FLOAT32NE : CUBEB_SAMPLE_S16NE;
	// params.rate = freq;
	// params.channels = channels;
	// params.layout = CUBEB_LAYOUT_UNDEFINED;
	// params.prefs = CUBEB_STREAM_PREF_NONE;
	//
	// uint32_t latency_frames = 0;
	// cubeb_get_min_latency(g_cubeb_context, &params, &latency_frames);
	//
	// cubeb_stream* stream = nullptr;
	// int rv = cubeb_stream_init(g_cubeb_context, &stream, "KytyPS5 AudioOut",
	//                            nullptr, nullptr,  // input
	//                            nullptr, &params,   // output
	//                            latency_frames,
	//                            CubebDataCallback, CubebStateCallback,
	//                            out_state);
	// if (rv != CUBEB_OK) {
	//     LOGF("Cubeb: cubeb_stream_init failed: %d\n", rv);
	//     return false;
	// }
	//
	// out_state->cubeb_stream = stream;
	// out_state->sample_rate = freq;
	// out_state->channels = channels;
	// out_state->is_float = (format >= CubebFormat::FloatMono);
	// out_state->samples_num = samples_num;
	// out_state->paused = true;
	//
	// // Allocate ring buffer (2x the expected buffer size for safety)
	// const auto bytes_per_frame = channels * (out_state->is_float ? 4 : 2);
	// out_state->ring_capacity = samples_num * bytes_per_frame * 4;
	// out_state->ring_buffer.resize(out_state->ring_capacity);
	// out_state->ring_write_pos = 0;
	// out_state->ring_read_pos = 0;
	//
	// cubeb_stream_start(stream);
	// LOGF("Cubeb: opened device (%u Hz, %u ch, %s, latency=%u frames)\n",
	//      freq, channels, out_state->is_float ? "float32" : "s16", latency_frames);

	LOGF("Cubeb: OpenCubebDevice called but cubeb is not linked\n");
	return false;
}

void CloseCubebDevice(CubebPortState* state) {
	if (state == nullptr || state->cubeb_stream == nullptr) {
		return;
	}

	// When cubeb is linked:
	// cubeb_stream_stop(static_cast<cubeb_stream*>(state->cubeb_stream));
	// cubeb_stream_destroy(static_cast<cubeb_stream*>(state->cubeb_stream));
	state->cubeb_stream = nullptr;
	state->ring_buffer.clear();
	state->ring_capacity = 0;
}

bool CubebQueueAudio(CubebPortState* state, const void* data, uint32_t size) {
	if (state == nullptr || state->cubeb_stream == nullptr || data == nullptr || size == 0) {
		return false;
	}

	// Copy data into ring buffer
	const auto bytes_to_write = std::min(static_cast<uint64_t>(size),
					     state->ring_capacity - (state->ring_write_pos - state->ring_read_pos));

	// Handle ring buffer wrap-around
	const auto write_pos = state->ring_write_pos % state->ring_capacity;
	const auto first_chunk = std::min(bytes_to_write, state->ring_capacity - write_pos);

	std::memcpy(state->ring_buffer.data() + write_pos, data, first_chunk);
	if (bytes_to_write > first_chunk) {
		std::memcpy(state->ring_buffer.data(),
			    static_cast<const uint8_t*>(data) + first_chunk,
			    bytes_to_write - first_chunk);
	}

	state->ring_write_pos += bytes_to_write;
	return true;
}

void CubebPauseDevice(CubebPortState* state, bool pause) {
	if (state == nullptr || state->cubeb_stream == nullptr) {
		return;
	}

	// When cubeb is linked:
	// if (pause) {
	//     cubeb_stream_stop(static_cast<cubeb_stream*>(state->cubeb_stream));
	// } else {
	//     cubeb_stream_start(static_cast<cubeb_stream*>(state->cubeb_stream));
	// }
	state->paused = pause;
}

} // namespace Libs::Audio
