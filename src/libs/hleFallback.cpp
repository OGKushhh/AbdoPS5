// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-024: Pure-HLE fallback mode — implementation.

#include "libs/hleFallback.h"

#include "common/logging/log.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace Libs::HleFallback {

// Mapping from firmware module filenames to HLE library names.
// These are the modules that games commonly request via
// sceKernelLoadStartModule(). Each has a corresponding HLE
// implementation in src/libs/lib*.cpp.
static const std::unordered_map<std::string, std::string> kModuleMap = {
    // Core system modules
    {"libc.prx",               "LibcInternal"},
    {"libkernel.prx",          "libkernel"},
    {"libSceLibcInternal.prx", "LibcInternal"},

    // Audio/video
    {"libSceAudio.prx",        "Audio"},
    {"libSceAudio2.prx",       "Audio2"},
    {"libSceAjm.prx",          "Ajm"},

    // Graphics
    {"libSceAgcDriver.prx",    "Graphics5"},
    {"libSceVideoOut.prx",     "VideoOut"},

    // Input
    {"libScePad.prx",          "Pad"},

    // Font
    {"libSceFont.prx",         "Font"},
    {"libSceFontFt.prx",       "FontFt"},

    // Network
    {"libSceNet.prx",          "Net"},
    {"libSceNetCtl.prx",       "NetCtl"},

    // Save data
    {"libSceSaveData.prx",     "SaveData"},

    // System
    {"libSceSysmodule.prx",    "Sysmodule"},
    {"libSceSystemService.prx","SystemService"},
    {"libSceRtc.prx",          "Rtc"},
    {"libSceJson.prx",         "Json2"},
    {"libScePngDec.prx",       "PngDec"},
    {"libScePlayGo.prx",       "PlayGo"},
    {"libSceShare.prx",        "Share"},
    {"libSceUserService.prx",  "UserService"},
    {"libSceCommonDialog.prx", "Dialog"},
    {"libSceAppContent.prx",   "AppContent"},
    {"libSceUlt.prx",          "Ult"},
    {"libSceCes.prx",          "Ces"},
    {"libScePsml.prx",         "Psml"},

    // Video decode
    {"libSceVideoDec2.prx",    "VideoDec2"},

    // Content search/export
    {"libSceContentDelete.prx",  "ContentDelete"},
    {"libSceContentExport.prx",  "ContentExport"},
    {"libSceContentSearch.prx",  "ContentSearch"},
};

// State tracking
static std::mutex g_mutex;
static std::vector<std::string> g_fallbacks_used;
static bool g_warned = false;

std::string_view GetHleLibraryName(const std::string& module_name) {
    // Extract just the filename (strip path)
    auto pos = module_name.find_last_of('/');
    std::string filename = (pos != std::string::npos)
        ? module_name.substr(pos + 1)
        : module_name;

    auto it = kModuleMap.find(filename);
    if (it != kModuleMap.end()) {
        return it->second;
    }
    return {};
}

void RecordFallback(const std::string& module_name) {
    std::lock_guard lock(g_mutex);
    g_fallbacks_used.push_back(module_name);

    if (!g_warned) {
        g_warned = true;
        LOGF("HLE Fallback: using built-in HLE for missing firmware modules.\n"
             "  Games may have reduced compatibility without firmware dumps.\n"
             "  This warning appears once; subsequent fallbacks are logged silently.\n");
    }

    LOGF("HLE Fallback: '%s' not found on disk — using HLE implementation\n",
         module_name.c_str());
}

bool UsedAnyFallback() {
    std::lock_guard lock(g_mutex);
    return !g_fallbacks_used.empty();
}

uint32_t GetFallbackCount() {
    std::lock_guard lock(g_mutex);
    return static_cast<uint32_t>(g_fallbacks_used.size());
}

std::string GetFallbackSummary() {
    std::lock_guard lock(g_mutex);
    std::string summary = "HLE fallback modules used:\n";
    for (const auto& name : g_fallbacks_used) {
        summary += "  - " + name + "\n";
    }
    return summary;
}

void Reset() {
    std::lock_guard lock(g_mutex);
    g_fallbacks_used.clear();
    g_warned = false;
}

} // namespace Libs::HleFallback
