// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-024: Pure-HLE fallback mode.
//
// When a game calls sceKernelLoadStartModule("libc.prx") and the file
// doesn't exist on disk (no firmware dump), the emulator should fall
// back to its built-in HLE implementation instead of returning ENOENT.
//
// This module maps firmware module names to their HLE library
// equivalents. When a module file is missing, the fallback checks if
// an HLE library with the same exports is already registered. If so,
// it returns a fake module handle so the game thinks the module loaded
// successfully.
//
// The HLE libraries are already registered at startup via LIB_DEFINE
// in libs.cpp. This module just provides the mapping from firmware
// filenames to HLE library names, and a "did we fall back?" flag.

#ifndef KYTY_LIBS_HLE_FALLBACK_H_
#define KYTY_LIBS_HLE_FALLBACK_H_

#include "common/common.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Libs::HleFallback {

// Check if a module filename has an HLE fallback available.
// module_name: the filename the game requested (e.g. "libc.prx", "libSceFont.prx")
// Returns the HLE library name if available, or empty string if not.
[[nodiscard]] std::string_view GetHleLibraryName(const std::string& module_name);

// Record that a fallback was used (for logging + warning).
void RecordFallback(const std::string& module_name);

// Check if any fallbacks have been used since startup.
[[nodiscard]] bool UsedAnyFallback();

// Get the number of fallbacks used.
[[nodiscard]] uint32_t GetFallbackCount();

// Get the list of modules that fell back to HLE (for the warning log).
[[nodiscard]] std::string GetFallbackSummary();

// Reset the fallback tracking (for tests).
void Reset();

} // namespace Libs::HleFallback

#endif // KYTY_LIBS_HLE_FALLBACK_H_
