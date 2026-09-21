// SPDX-FileCopyrightText: Copyright 2026 KytyPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-011: Cubeb audio backend.
//
// Cubeb is Mozilla's cross-platform audio library used by Firefox, Citra,
// RPCS3, and Dolphin. It has meaningfully lower latency than SDL on
// Linux/PipeWire and better device routing on macOS.
//
// This file provides InitCubeb/ShutdownCubeb/OpenCubebDevice/CloseCubebDevice/
// CubebQueueAudio/CubebPauseDevice that mirror the existing SDL audio path.
// The backend is selected at runtime via Config::GetAudioBackend().
//
// Wired in commit b7aa2918 (Kyty-011). The cubeb submodule is fetched via
// FetchContent in 3rdparty/CMakeLists.txt; the kyty_emulator target links
// against it when USE_CUBEB=ON (the default). Set USE_CUBEB=OFF to disable.

#include "common/common.h"
#include "common/logging/log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(KYTY_USE_CUBEB)
#include <cubeb/cubeb.h>
#endif

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
#if defined(KYTY_USE_CUBEB)
        cubeb_stream* cubeb_stream = nullptr;
#else
        void*       cubeb_stream = nullptr; // unused when cubeb is not linked
#endif
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
        // Mutex guards ring positions + buffer (Cubeb's audio thread reads
        // while the emulator thread writes).
        std::mutex ring_mutex;
};

#if defined(KYTY_USE_CUBEB)
static cubeb* g_cubeb_context = nullptr;
#else
static void*  g_cubeb_context = nullptr;
#endif
static bool   g_cubeb_initialized = false;

bool InitCubeb() {
        if (g_cubeb_initialized) {
                return g_cubeb_context != nullptr;
        }
        g_cubeb_initialized = true;

#if defined(KYTY_USE_CUBEB)
        int rv = cubeb_init(&g_cubeb_context, "KytyPS5", nullptr);
        if (rv != CUBEB_OK) {
                LOGF("Cubeb: cubeb_init failed: %d\n", rv);
                g_cubeb_context = nullptr;
                return false;
        }
        LOGF("Cubeb: context initialized (backend: %s)\n",
             cubeb_get_backend_id(g_cubeb_context));
        return true;
#else
        LOGF("Cubeb: backend not compiled in (set USE_CUBEB=ON to enable). "
             "Falling back to SDL.\n");
        return false;
#endif
}

void ShutdownCubeb() {
#if defined(KYTY_USE_CUBEB)
        if (g_cubeb_context) {
                cubeb_destroy(g_cubeb_context);
                g_cubeb_context = nullptr;
        }
#else
        g_cubeb_context = nullptr;
#endif
        g_cubeb_initialized = false;
}

bool IsCubebAvailable() {
        if (!g_cubeb_initialized) {
                InitCubeb();
        }
        return g_cubeb_context != nullptr;
}

#if defined(KYTY_USE_CUBEB)
// Cubeb audio callback (called from Cubeb's audio thread)
static long CubebDataCallback(cubeb_stream* /*stream*/, void* user_data,
                              void const* /*input_buffer*/, void* output_buffer,
                              long frames) {
        auto* state = static_cast<CubebPortState*>(user_data);
        if (state == nullptr) {
                std::memset(output_buffer, 0, frames * 8 * 4); // worst case
                return frames;
        }

        const auto bytes_per_frame = state->channels * (state->is_float ? 4 : 2);
        const auto needed_bytes = static_cast<uint64_t>(frames) * bytes_per_frame;

        std::lock_guard<std::mutex> lock(state->ring_mutex);

        const auto filled =
            (state->ring_write_pos >= state->ring_read_pos)
                ? (state->ring_write_pos - state->ring_read_pos)
                : (state->ring_capacity - state->ring_read_pos + state->ring_write_pos);

        if (state->paused || filled < needed_bytes) {
                // Not enough data — output silence (avoids the Cubeb error path
                // that would happen if we returned < frames).
                std::memset(output_buffer, 0, needed_bytes);
                return frames;
        }

        // Copy from ring buffer to output (handle wrap-around)
        auto* dst = static_cast<uint8_t*>(output_buffer);
        const auto* src = state->ring_buffer.data();
        const auto read_pos = state->ring_read_pos % state->ring_capacity;
        const auto first_chunk = std::min(needed_bytes, state->ring_capacity - read_pos);
        std::memcpy(dst, src + read_pos, first_chunk);
        if (needed_bytes > first_chunk) {
                std::memcpy(dst + first_chunk, src, needed_bytes - first_chunk);
        }
        state->ring_read_pos += needed_bytes;
        return frames;
}

static void CubebStateCallback(cubeb_stream* /*stream*/, void* /*user_data*/, cubeb_state state) {
        const char* state_names[] = {"started", "stopped", "drained", "error", nullptr};
        int idx = static_cast<int>(state);
        LOGF("Cubeb: state changed: %s\n",
             (idx >= 0 && idx <= 3) ? state_names[idx] : "unknown");
}
#endif // KYTY_USE_CUBEB

bool OpenCubebDevice(uint32_t freq, uint32_t channels, CubebFormat format,
                     uint32_t samples_num, CubebPortState* out_state) {
        if (!IsCubebAvailable() || out_state == nullptr) {
                return false;
        }

#if !defined(KYTY_USE_CUBEB)
        LOGF("Cubeb: OpenCubebDevice called but cubeb is not compiled in\n");
        return false;
#else
        cubeb_stream_params params{};
        params.format = (format >= CubebFormat::FloatMono)
                            ? CUBEB_SAMPLE_FLOAT32NE
                            : CUBEB_SAMPLE_S16NE;
        params.rate = freq;
        params.channels = static_cast<uint32_t>(channels);
        params.layout = CUBEB_LAYOUT_UNDEFINED;
        params.prefs = CUBEB_STREAM_PREF_NONE;

        uint32_t latency_frames = 0;
        int rv = cubeb_get_min_latency(g_cubeb_context, &params, &latency_frames);
        if (rv != CUBEB_OK) {
                LOGF("Cubeb: cubeb_get_min_latency failed: %d (using default)\n", rv);
                latency_frames = samples_num;
        }

        cubeb_stream* stream = nullptr;
        rv = cubeb_stream_init(g_cubeb_context, &stream, "KytyPS5 AudioOut",
                               nullptr, nullptr,  // input
                               nullptr, &params,  // output
                               latency_frames,
                               CubebDataCallback, CubebStateCallback,
                               out_state);
        if (rv != CUBEB_OK) {
                LOGF("Cubeb: cubeb_stream_init failed: %d\n", rv);
                return false;
        }

        out_state->cubeb_stream = stream;
        out_state->sample_rate = freq;
        out_state->channels = channels;
        out_state->is_float = (format >= CubebFormat::FloatMono);
        out_state->samples_num = samples_num;
        out_state->paused = true;

        // Allocate ring buffer (4x the expected buffer size for safety margin)
        const auto bytes_per_frame = channels * (out_state->is_float ? 4 : 2);
        out_state->ring_capacity = static_cast<uint64_t>(samples_num) * bytes_per_frame * 4;
        out_state->ring_buffer.resize(out_state->ring_capacity);
        out_state->ring_write_pos = 0;
        out_state->ring_read_pos = 0;

        rv = cubeb_stream_start(stream);
        if (rv != CUBEB_OK) {
                LOGF("Cubeb: cubeb_stream_start failed: %d\n", rv);
                cubeb_stream_destroy(stream);
                out_state->cubeb_stream = nullptr;
                return false;
        }

        LOGF("Cubeb: opened device (%u Hz, %u ch, %s, latency=%u frames)\n",
             freq, channels, out_state->is_float ? "float32" : "s16", latency_frames);
        return true;
#endif
}

void CloseCubebDevice(CubebPortState* state) {
        if (state == nullptr || state->cubeb_stream == nullptr) {
                return;
        }

#if defined(KYTY_USE_CUBEB)
        cubeb_stream_stop(state->cubeb_stream);
        cubeb_stream_destroy(state->cubeb_stream);
#endif
        state->cubeb_stream = nullptr;
        std::lock_guard<std::mutex> lock(state->ring_mutex);
        state->ring_buffer.clear();
        state->ring_capacity = 0;
        state->ring_write_pos = 0;
        state->ring_read_pos = 0;
}

bool CubebQueueAudio(CubebPortState* state, const void* data, uint32_t size) {
        if (state == nullptr || state->cubeb_stream == nullptr || data == nullptr || size == 0) {
                return false;
        }

#if !defined(KYTY_USE_CUBEB)
        return false;
#else
        std::lock_guard<std::mutex> lock(state->ring_mutex);

        // How much free space is available in the ring?
        const auto filled =
            (state->ring_write_pos >= state->ring_read_pos)
                ? (state->ring_write_pos - state->ring_read_pos)
                : (state->ring_capacity - state->ring_read_pos + state->ring_write_pos);
        const auto free_bytes = state->ring_capacity - filled;
        const auto bytes_to_write = std::min(static_cast<uint64_t>(size), free_bytes);

        if (bytes_to_write == 0) {
                return false; // ring buffer full — drop this packet
        }

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
        return bytes_to_write == size;
#endif
}

void CubebPauseDevice(CubebPortState* state, bool pause) {
        if (state == nullptr || state->cubeb_stream == nullptr) {
                return;
        }

        state->paused = pause;

#if defined(KYTY_USE_CUBEB)
        if (pause) {
                cubeb_stream_stop(state->cubeb_stream);
        } else {
                cubeb_stream_start(state->cubeb_stream);
        }
#endif
}

} // namespace Libs::Audio
