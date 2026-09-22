// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-022: Screenshot module unit tests.

#include "graphics/presentation/screenshot.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

using Libs::Graphics::Screenshot;

int g_test_failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "ScreenshotTests: FAILED: %s\n", message);
        ++g_test_failures;
    } else {
        std::printf("ScreenshotTests: passed: %s\n", message);
    }
}

void TestRequest() {
    Screenshot s;
    Check(!s.IsRequested(), "Starts with no request");
    s.Request();
    Check(s.IsRequested(), "Request sets flag");
}

void TestGenerateFilename() {
    auto filename = Screenshot::GenerateFilename("PPSA04288");
    Check(filename.find("PPSA04288") != std::string::npos, "Filename contains title_id");
    Check(filename.find(".png") != std::string::npos, "Filename has .png extension");
    Check(filename.find("screenshot_") == 0, "Filename starts with screenshot_");

    auto empty = Screenshot::GenerateFilename("");
    Check(empty.find("unknown") != std::string::npos, "Empty title_id uses 'unknown'");
}

void TestSaveToPng() {
    // Create a 4x4 RGBA test image (all red).
    constexpr uint32_t W = 4, H = 4;
    uint8_t pixels[W * H * 4];
    for (size_t i = 0; i < W * H; ++i) {
        pixels[i * 4 + 0] = 255; // R
        pixels[i * 4 + 1] = 0;   // G
        pixels[i * 4 + 2] = 0;   // B
        pixels[i * 4 + 3] = 255; // A
    }

    const std::string dir = "test_screenshots";
    auto path = Screenshot::SaveToPng(W, H, pixels, dir);
    Check(!path.empty(), "SaveToPng returns non-empty path");
    Check(std::filesystem::exists(path), "PNG file was created");
    Check(std::filesystem::file_size(path) > 0, "PNG file is not empty");

    // Test invalid parameters.
    Check(Screenshot::SaveToPng(0, 0, nullptr, dir).empty(), "SaveToPng rejects null/zero params");

    // Clean up.
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    TestRequest();
    TestGenerateFilename();
    TestSaveToPng();

    if (g_test_failures == 0) {
        std::printf("\nScreenshotTests: ALL PASSED\n");
        return 0;
    }
    std::printf("\nScreenshotTests: %d FAILED\n", g_test_failures);
    return 1;
}
