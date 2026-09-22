// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-022: Screenshot module — implementation.

#include "graphics/presentation/screenshot.h"

#include "common/logging/log.h"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <zlib.h>

namespace Libs::Graphics {

void Screenshot::Request() {
    m_requested = true;
}

std::string Screenshot::GenerateFilename(const std::string& title_id) {
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

// Minimal PNG encoder for RGBA8 data.
// PNG format: signature + IHDR + IDAT + IEND chunks.
// Uses zlib for IDAT compression (already linked in kyty_emulator).
namespace {

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    return static_cast<uint32_t>(crc32(crc, data, static_cast<uInt>(len)));
}

void write_u32_be(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back((val >> 24) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back(val & 0xFF);
}

void write_chunk(std::vector<uint8_t>& buf, const char* type,
                 const uint8_t* data, size_t len) {
    write_u32_be(buf, static_cast<uint32_t>(len));
    uint32_t crc = crc32_update(0, reinterpret_cast<const uint8_t*>(type), 4);
    buf.insert(buf.end(), type, type + 4);
    if (data && len > 0) {
        buf.insert(buf.end(), data, data + len);
        crc = crc32_update(crc, data, len);
    }
    write_u32_be(buf, crc);
}

} // namespace

std::string Screenshot::SaveToPng(uint32_t width, uint32_t height,
                                   const uint8_t* rgba_data,
                                   const std::string& output_dir) {
    if (rgba_data == nullptr || width == 0 || height == 0) {
        LOGF("Screenshot: invalid parameters (w=%u h=%u data=%p)\n",
             width, height, static_cast<const void*>(rgba_data));
        return "";
    }

    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);

    const auto filename = GenerateFilename("emulator");
    const auto filepath = std::filesystem::path(output_dir) / filename;

    // Build the raw image data with PNG filter bytes (one per row).
    // Each row starts with a filter type byte (0 = None).
    const size_t row_bytes = width * 4;
    std::vector<uint8_t> raw_data((row_bytes + 1) * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw_data[y * (row_bytes + 1)] = 0; // filter: None
        std::memcpy(raw_data.data() + y * (row_bytes + 1) + 1,
                    rgba_data + y * row_bytes, row_bytes);
    }

    // Compress with zlib.
    uLong compressed_size = compressBound(static_cast<uLong>(raw_data.size()));
    std::vector<uint8_t> compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size,
                  raw_data.data(), static_cast<uLong>(raw_data.size()),
                  Z_BEST_SPEED) != Z_OK) {
        LOGF("Screenshot: zlib compress failed\n");
        return "";
    }
    compressed.resize(compressed_size);

    // Build PNG file.
    std::vector<uint8_t> png;
    // PNG signature
    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), signature, signature + 8);

    // IHDR chunk
    uint8_t ihdr[13] = {};
    ihdr[0] = (width >> 24) & 0xFF;
    ihdr[1] = (width >> 16) & 0xFF;
    ihdr[2] = (width >> 8) & 0xFF;
    ihdr[3] = width & 0xFF;
    ihdr[4] = (height >> 24) & 0xFF;
    ihdr[5] = (height >> 16) & 0xFF;
    ihdr[6] = (height >> 8) & 0xFF;
    ihdr[7] = height & 0xFF;
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 6;   // color type: RGBA
    ihdr[10] = 0;  // compression: deflate
    ihdr[11] = 0;  // filter: adaptive
    ihdr[12] = 0;  // interlace: none
    write_chunk(png, "IHDR", ihdr, 13);

    // IDAT chunk
    write_chunk(png, "IDAT", compressed.data(), compressed.size());

    // IEND chunk
    write_chunk(png, "IEND", nullptr, 0);

    // Write to file.
    FILE* file = std::fopen(filepath.string().c_str(), "wb");
    if (file == nullptr) {
        LOGF("Screenshot: cannot open %s for writing\n", filepath.string().c_str());
        return "";
    }
    const size_t written = std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);

    if (written != png.size()) {
        LOGF("Screenshot: short write to %s (%zu of %zu)\n",
             filepath.string().c_str(), written, png.size());
        return "";
    }

    LOGF("Screenshot: saved %ux%u PNG to %s (%zu bytes)\n",
         width, height, filepath.string().c_str(), png.size());

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
