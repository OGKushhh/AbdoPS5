// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-014: DualSense haptic audio player — implementation.

#include "libs/hapticPlayer.h"

#include "common/logging/log.h"

#include <SDL2/SDL_gamecontroller.h>
#include <cstring>
#include <fstream>

namespace Libs::Controller {

bool HapticPlayer::LoadHapticFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
                LOGF("HapticPlayer: cannot open %s\n", path.c_str());
                return false;
        }

        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (!file) {
                LOGF("HapticPlayer: failed to read %s\n", path.c_str());
                return false;
        }

        return LoadHapticData(data.data(), data.size());
}

bool HapticPlayer::LoadHapticData(const uint8_t* data, size_t size) {
        if (data == nullptr || size == 0) {
                return false;
        }

        // Queue all samples.
        for (size_t i = 0; i < size; ++i) {
                m_buffer.push(data[i]);
        }

        LOGF("HapticPlayer: loaded %zu haptic samples (%.1f seconds at %u Hz)\n",
             size, static_cast<float>(size) / HAPTIC_SAMPLE_RATE, HAPTIC_SAMPLE_RATE);
        return true;
}

void HapticPlayer::QueueSamples(const uint8_t* samples, size_t count) {
        if (samples == nullptr || count == 0) {
                return;
        }

        // Limit the buffer to ~1 second of haptic data (3000 samples).
        // If the buffer is full, drop old samples (haptic data is
        // time-sensitive — late samples are useless and would cause
        // latency buildup).
        constexpr size_t MAX_BUFFER = HAPTIC_SAMPLE_RATE;
        while (m_buffer.size() > MAX_BUFFER - count && !m_buffer.empty()) {
                m_buffer.pop();
        }

        for (size_t i = 0; i < count; ++i) {
                m_buffer.push(samples[i]);
        }
}

size_t HapticPlayer::Pump(void* pad_ptr) {
        auto* pad = static_cast<SDL_GameController*>(pad_ptr);
        if (pad == nullptr) {
                return 0;
        }

        // Build the DualSense effect packet.
        // The haptic audio is sent as part of the same 32-byte packet
        // as trigger effects, but we use a dedicated packet here with
        // only the haptic motor bits enabled.
        //
        // DualSense effect packet format (32 bytes):
        //   byte 0:  enable_bits (bit 0 = motor L, bit 1 = motor R,
        //           bit 2 = right trigger, bit 3 = left trigger)
        //   bytes 1-9: reserved
        //   bytes 10-20: right trigger effect (11 bytes)
        //   bytes 21-31: left trigger effect (11 bytes)
        //
        // For haptic audio, we set bits 0+1 (both motors) and leave
        // the trigger effect bytes as 0 (no trigger effect).
        //
        // However, the actual DualSense haptic audio is NOT sent via
        // this simple motor path — it requires a separate audio report
        // with PCM samples. SDL3's SDL_GameControllerSendEffect only
        // supports the motor + trigger effect path.
        //
        // For now, we convert haptic samples to rumble intensity:
        // each chunk of 30 samples is averaged and mapped to the
        // small motor (high-frequency) rumble. This is a rough
        // approximation — real DualSense haptic audio would require
        // a lower-level HID interface to send PCM data directly.
        //
        // This approximation is good enough for basic haptic effects
        // (explosions, impacts) but won't reproduce fine-grained
        // textures (rain, footsteps).

        uint8_t chunk[HAPTIC_CHUNK_SAMPLES];
        size_t samples_sent = 0;

        // Fill the chunk from the buffer (or silence if empty).
        for (size_t i = 0; i < HAPTIC_CHUNK_SAMPLES; ++i) {
                if (!m_buffer.empty()) {
                        chunk[i] = m_buffer.front();
                        m_buffer.pop();
                        ++samples_sent;
                } else {
                        chunk[i] = HAPTIC_SILENCE;
                }
        }

        // If we sent no real samples, the buffer is empty — send
        // silence to stop the motors.
        if (samples_sent == 0) {
                SDL_GameControllerRumble(pad, 0, 0, 10);  // 10ms silence
                return 0;
        }

        // Compute average amplitude of the chunk.
        // Haptic data is 8-bit unsigned (128 = silence, 0/255 = max).
        // Convert to 0-1 range: abs(sample - 128) / 128.
        float avg_amplitude = 0.0f;
        for (size_t i = 0; i < HAPTIC_CHUNK_SAMPLES; ++i) {
                const int16_t centered = static_cast<int16_t>(chunk[i]) - HAPTIC_SILENCE;
                avg_amplitude += static_cast<float>(centered < 0 ? -centered : centered);
        }
        avg_amplitude /= static_cast<float>(HAPTIC_CHUNK_SAMPLES * HAPTIC_SILENCE);

        // Map to rumble: use the small motor (high-frequency) for
        // haptic effects. Scale to 0-0xFFFF.
        const uint16_t rumble_intensity = static_cast<uint16_t>(avg_amplitude * 0xFFFF);
        SDL_GameControllerRumble(pad, 0, rumble_intensity, 10);  // 10ms rumble

        m_total_played += samples_sent;
        return samples_sent;
}

void HapticPlayer::Stop() {
        while (!m_buffer.empty()) {
                m_buffer.pop();
        }
}

void HapticPlayer::Reset() {
        Stop();
        m_total_played = 0;
}

} // namespace Libs::Controller
