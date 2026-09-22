// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-014: DualSense haptic audio player.
//
// The DualSense controller has voice-coil haptic actuators (not just
// rumble motors) that can play PCM audio waveforms. Games send haptic
// data as raw audio samples through the controller's effect channel.
//
// This module provides:
// - HapticPlayer class: buffers PCM samples and sends them via
//   SDL_GameControllerSendEffect in chunks the DualSense can accept
// - LoadHapticFile(): loads a .haptic file (raw PCM, 3000 Hz, 8-bit,
//   mono — the DualSense's native haptic sample rate)
// - PlayHaptic(): queues haptic samples for playback
// - StopHaptic(): stops all haptic playback
//
// The DualSense accepts haptic audio in 10ms chunks (30 samples at
// 3000 Hz) interleaved with trigger effects in the same 32-byte packet.
// We use the SDL_GameControllerSendEffect path, same as trigger effects.
//
// Format reference: DualSense haptic data is sent as part of the
// feature report. SDL3 abstracts this via SDL_GameControllerSendEffect
// with a custom effect struct.

#ifndef KYTY_LIBS_HAPTIC_PLAYER_H_
#define KYTY_LIBS_HAPTIC_PLAYER_H_

#include "common/common.h"

#include <cstdint>
#include <queue>
#include <string>
#include <vector>

// Forward-declare SDL type to avoid pulling the SDL2 header into every
// file that includes this header. The implementation file includes
// the real SDL2/SDL_gamecontroller.h.
struct SDL_GameController;

namespace Libs::Controller {

// DualSense haptic audio format:
// - Sample rate: 3000 Hz (3 kHz)
// - Bit depth: 8-bit unsigned (0-255, 128 = silence)
// - Channels: 1 (mono, sent to both actuators)
constexpr uint32_t HAPTIC_SAMPLE_RATE = 3000;
constexpr uint32_t HAPTIC_CHUNK_SAMPLES = 30;  // 10ms at 3 kHz
constexpr uint8_t  HAPTIC_SILENCE = 128;

class HapticPlayer {
public:
        HapticPlayer() = default;
        ~HapticPlayer() = default;

        // Load haptic data from a .haptic file (raw PCM 8-bit mono 3000 Hz).
        // Returns true on success. The file format is just raw bytes —
        // no header. Each byte is one sample.
        bool LoadHapticFile(const std::string& path);

        // Load haptic data from a memory buffer (same format as the file).
        // Useful for games that send haptic data in-memory.
        bool LoadHapticData(const uint8_t* data, size_t size);

        // Queue haptic samples for playback. The samples are buffered
        // and sent to the controller in 10ms chunks when Pump() is called.
        // If the buffer is full, old samples are dropped (haptic data
        // is time-sensitive — late samples are useless).
        void QueueSamples(const uint8_t* samples, size_t count);

        // Send the next chunk of haptic data to the controller.
        // Call this every ~10ms from the main loop. If no samples are
        // queued, sends silence (which stops the actuators).
        // Returns the number of samples sent (0-30).
        size_t Pump(SDL_GameController* pad);

        // Stop all haptic playback and clear the buffer.
        void Stop();

        // Get the number of samples currently buffered.
        [[nodiscard]] size_t GetBufferedSamples() const { return m_buffer.size(); }

        // Get the total number of samples played since the last reset.
        [[nodiscard]] uint64_t GetTotalSamplesPlayed() const { return m_total_played; }

        // Reset all state.
        void Reset();

private:
        // Ring buffer of haptic samples (8-bit unsigned PCM).
        // std::queue is fine — haptic data is low-volume (3000 samples/sec).
        std::queue<uint8_t> m_buffer;

        // Total samples played (for debugging / stats).
        uint64_t m_total_played = 0;
};

} // namespace Libs::Controller

#endif // KYTY_LIBS_HAPTIC_PLAYER_H_
