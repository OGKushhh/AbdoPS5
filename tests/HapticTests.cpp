// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-014: HapticPlayer unit tests.

#include "libs/hapticPlayer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using Libs::Controller::HapticPlayer;
using Libs::Controller::HAPTIC_CHUNK_SAMPLES;
using Libs::Controller::HAPTIC_SILENCE;

int g_test_failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "HapticTests: FAILED: %s\n", message);
        ++g_test_failures;
    } else {
        std::printf("HapticTests: passed: %s\n", message);
    }
}

void TestLoadAndQueue() {
    HapticPlayer player;

    // Create test data: 60 samples of max amplitude alternating with silence.
    uint8_t data[60];
    for (int i = 0; i < 60; i++) {
        data[i] = (i % 2 == 0) ? 255 : HAPTIC_SILENCE;
    }

    Check(player.LoadHapticData(data, 60), "LoadHapticData accepts 60 samples");
    Check(player.GetBufferedSamples() == 60, "Buffer has 60 samples after load");

    // Queue more samples.
    player.QueueSamples(data, 30);
    Check(player.GetBufferedSamples() == 90, "Buffer has 90 samples after queue");

    // Queue enough to overflow (should cap at ~3000).
    std::vector<uint8_t> big(4000, 255);
    player.QueueSamples(big.data(), 4000);
    Check(player.GetBufferedSamples() <= 3000, "Buffer caps at ~3000 samples on overflow");
}

void TestPump() {
    HapticPlayer player;

    // Pump with no data — should return 0 (silence).
    Check(player.Pump(nullptr) == 0, "Pump with null pad returns 0");

    // Load 30 samples (one chunk) and pump.
    uint8_t data[HAPTIC_CHUNK_SAMPLES];
    std::memset(data, 255, HAPTIC_CHUNK_SAMPLES);
    player.LoadHapticData(data, HAPTIC_CHUNK_SAMPLES);

    Check(player.GetBufferedSamples() == HAPTIC_CHUNK_SAMPLES, "Buffer has 30 samples");
    // Can't test actual SDL rumble without a controller, but the Pump
    // function should still process the buffer and return the count.
    // Since pad is nullptr, it returns 0 (early exit).
    Check(player.Pump(nullptr) == 0, "Pump with null pad processes 0 samples");
    // But the buffer should still be consumed... actually no, Pump
    // returns early if pad is null. Let's check buffer is still full.
    Check(player.GetBufferedSamples() == HAPTIC_CHUNK_SAMPLES, "Buffer unchanged when pad is null");
}

void TestStopAndReset() {
    HapticPlayer player;

    uint8_t data[100];
    std::memset(data, 200, 100);
    player.LoadHapticData(data, 100);
    Check(player.GetBufferedSamples() == 100, "Loaded 100 samples");

    player.Stop();
    Check(player.GetBufferedSamples() == 0, "Stop clears buffer");

    player.LoadHapticData(data, 50);
    Check(player.GetTotalSamplesPlayed() == 0, "Total played is 0 before any Pump");

    player.Reset();
    Check(player.GetBufferedSamples() == 0, "Reset clears buffer");
    Check(player.GetTotalSamplesPlayed() == 0, "Reset clears total played");
}

} // namespace

int main() {
    TestLoadAndQueue();
    TestPump();
    TestStopAndReset();

    if (g_test_failures == 0) {
        std::printf("\nHapticTests: ALL PASSED\n");
        return 0;
    }
    std::printf("\nHapticTests: %d FAILED\n", g_test_failures);
    return 1;
}
