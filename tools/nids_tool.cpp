// SPDX-FileCopyrightText: Copyright 2026 KytyPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-004: Standalone NID database query tool.
// Usage:
//   kytyps5-nids <NID>          — look up a NID hash
//   kytyps5-nids --name <name>  — reverse lookup by function name
//   kytyps5-nids --list         — list all NIDs
//   kytyps5-nids --stats        — show statistics

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct NidEntry {
    std::string nid;
    std::string name;
    std::string source_file;
};

std::vector<NidEntry> LoadDatabase(const std::string& path) {
    std::vector<NidEntry> entries;
    std::ifstream file(path);
    if (!file) {
        return entries;
    }
    std::string line;
    std::getline(file, line); // skip header
    while (std::getline(file, line)) {
        auto pos1 = line.find(',');
        if (pos1 == std::string::npos) continue;
        auto pos2 = line.find(',', pos1 + 1);
        NidEntry entry;
        entry.nid = line.substr(0, pos1);
        entry.name = line.substr(pos1 + 1, pos2 - pos1 - 1);
        entry.source_file = pos2 != std::string::npos ? line.substr(pos2 + 1) : "";
        entries.push_back(entry);
    }
    return entries;
}

void PrintUsage() {
    std::printf("kytyps5-nids — PS5 NID database query tool (Kyty-004)\n\n");
    std::printf("Usage:\n");
    std::printf("  kytyps5-nids <NID>           Look up a NID hash\n");
    std::printf("  kytyps5-nids --name <name>   Reverse lookup by function name\n");
    std::printf("  kytyps5-nids --list          List all NIDs\n");
    std::printf("  kytyps5-nids --stats         Show database statistics\n");
    std::printf("  kytyps5-nids --help          Show this help\n");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string db_path = "data/nids.csv";
    auto entries = LoadDatabase(db_path);
    if (entries.empty()) {
        db_path = "nids.csv";
        entries = LoadDatabase(db_path);
    }
    if (entries.empty()) {
        std::fprintf(stderr, "Error: cannot load NID database. Expected data/nids.csv or nids.csv\n");
        return 1;
    }

    std::string_view arg = argv[1];

    if (arg == "--help" || arg == "-h") {
        PrintUsage();
        return 0;
    }

    if (arg == "--stats") {
        std::printf("NID Database Statistics\n");
        std::printf("  Total entries: %zu\n", entries.size());
        std::unordered_set<std::string> unique_nids;
        std::unordered_set<std::string> unique_files;
        for (const auto& e : entries) {
            unique_nids.insert(e.nid);
            unique_files.insert(e.source_file);
        }
        std::printf("  Unique NIDs:  %zu\n", unique_nids.size());
        std::printf("  Source files: %zu\n", unique_files.size());
        return 0;
    }

    if (arg == "--list") {
        for (const auto& e : entries) {
            std::printf("%s = %s (%s)\n", e.nid.c_str(), e.name.c_str(), e.source_file.c_str());
        }
        return 0;
    }

    if (arg == "--name") {
        if (argc < 3) {
            std::fprintf(stderr, "Error: --name requires a function name argument\n");
            return 1;
        }
        std::string search = argv[2];
        bool found = false;
        for (const auto& e : entries) {
            if (e.name.find(search) != std::string::npos) {
                std::printf("%s = %s (%s)\n", e.nid.c_str(), e.name.c_str(), e.source_file.c_str());
                found = true;
            }
        }
        if (!found) {
            std::printf("No matches found for '%s'\n", search.c_str());
        }
        return 0;
    }

    // Default: treat arg as a NID hash
    bool found = false;
    for (const auto& e : entries) {
        if (e.nid == arg) {
            std::printf("%s = %s (%s)\n", e.nid.c_str(), e.name.c_str(), e.source_file.c_str());
            found = true;
        }
    }
    if (!found) {
        std::printf("NID '%s' not found in database\n", std::string(arg).c_str());
        return 1;
    }
    return 0;
}
