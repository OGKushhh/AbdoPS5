// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-022: Screenshot module — implementation.

#include "graphics/presentation/screenshot.h"

#include "common/logging/log.h"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <memory>

// stb_image_write for PNG encoding (already in 3rdparty/stb)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO  // we use stbi_write_png_to_func
#include <stb_image_write.h>

namespace Libs::Graphics {

void Screenshot::Request() {
    m_requested = true;
}

std::string Screenshot::GenerateFilename(const std::string& title_id) {
    // Generate: screenshot_<title_id>_<YYYYMMDD_HHMMSS>.png
    std::time_t now = std::time(nullptr);
    std::tm tm_buf {};
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char timestamp[32] {};
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_buf);

    std::string tid = title_id.empty() ? "unknown" : title_id;
    return "screenshot_" + tid + "_" + timestamp + ".png";
}

std::string Screenshot::SaveToPng(uint32_t width, uint32_t height,
                                   const uint8_t* rgba_data,
                                   const std::string& output_dir) {
    if (rgba_data == nullptr || width == 0 || height == 0) {
        LOGF("Screenshot: invalid parameters (w=%u h=%u data=%p)\n",
             width, height, static_cast<const void*>(rgba_data));
        return "";
    }

    // Create output directory if it doesn't exist.
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    // Ignore errors — the file open below will fail with a clear message.

    // Generate filename.
    // For standalone captures without a game context, use "emulator" as title_id.
    const auto filename = GenerateFilename("emulator");
    const auto filepath = std::filesystem::path(output_dir) / filename;

    // Write PNG using stb_image_write.
    // stbi_write_png writes a complete PNG file.
    // stride_in_bytes = width * 4 (RGBA = 4 bytes per pixel).
    const int stride = static_cast<int>(width) * 4;
    const int result = stbi_write_png(filepath.string().c_str(),
                                       static_cast<int>(width),
                                       static_cast<int>(height),
                                       4, // RGBA
                                       rgba_data,
                                       stride);

    if (result == 0) {
        LOGF("Screenshot: stbi_write_png failed for %s\n", filepath.string().c_str());
        return "";
    }

    LOGF("Screenshot: saved %ux%u PNG to %s (%zu bytes)\n",
         width, height, filepath.string().c_str(),
         static_cast<size_t>(width) * height * 4);

    return filepath.string();
}

// Global singleton accessor.
namespace {
std::unique_ptr<Screenshot> g_screenshot;
}

Screenshot* GetScreenshot() {
    return g_screenshot.get();
}

void InitializeScreenshot() {
    if (g_screenshot != nullptr) {
        return;
    }
    g_screenshot = std::make_unique<Screenshot>();
    LOGF("Screenshot: initialized (Alt+F12 or ShareCaptureScreenshot to capture)\n");
}

} // namespace Libs::Graphics
