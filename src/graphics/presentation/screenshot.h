// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-022: Screenshot module.
//
// Captures the emulator's rendered output (the Vulkan swapchain image)
// and saves it as a PNG file to the screenshots/ directory.
//
// Triggers:
// 1. Programmatic — games call ShareCaptureScreenshot() (libShare.cpp)
// 2. Hotkey — Alt+F12 (handled by SDL event loop in the window)
// 3. CLI — --screenshot (captures one frame then exits)
//
// The capture happens after the frame is presented. We use Vulkan's
// vkCmdCopyImage to copy the swapchain image to a host-visible buffer,
// then read it back and encode as PNG using stb_image_write (already
// in the 3rdparty/stb dependency).
//
// PNG format: 32-bit RGBA, matching the swapchain format (B8G8R8A8).

#ifndef KYTY_GRAPHICS_PRESENTATION_SCREENSHOT_H_
#define KYTY_GRAPHICS_PRESENTATION_SCREENSHOT_H_

#include "common/common.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Libs::Graphics {

class Screenshot {
public:
    Screenshot() = default;
    ~Screenshot() = default;

    // Trigger a screenshot on the next present.
    // Thread-safe — can be called from any thread (game thread, hotkey
    // handler, CLI flag handler).
    void Request();

    // Check if a screenshot has been requested.
    [[nodiscard]] bool IsRequested() const { return m_requested; }

    // Save raw RGBA pixel data to a PNG file.
    // width/height: image dimensions
    // data: raw RGBA8 pixel data (4 bytes per pixel, row-major)
    // output_dir: directory to save to (default: "screenshots")
    // Returns the path of the saved file, or empty string on failure.
    static std::string SaveToPng(uint32_t width, uint32_t height,
                                  const uint8_t* rgba_data,
                                  const std::string& output_dir = "screenshots");

    // Generate a screenshot filename with timestamp + title_id.
    // Format: screenshot_<title_id>_<YYYYMMDD_HHMMSS>.png
    static std::string GenerateFilename(const std::string& title_id);

    // Get the default screenshot directory.
    [[nodiscard]] static std::string GetDefaultDir() { return "screenshots"; }

private:
    bool m_requested = false;
};

// Global accessor for the screenshot singleton.
[[nodiscard]] Screenshot* GetScreenshot();
void InitializeScreenshot();

} // namespace Libs::Graphics

#endif // KYTY_GRAPHICS_PRESENTATION_SCREENSHOT_H_
