// SPDX-FileCopyrightText: Copyright 2026 KytyPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-012: Standalone param.json/param.sfo info tool.
// Reads a PS5 param.json file and prints all key-value pairs.
// PS5 uses param.json (JSON format) instead of PS4's param.sfo (binary format).
//
// Usage:
//   kytyps5-param-sfo <param.json>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char* argv[]) {
	if (argc < 2) {
		std::printf("kytyps5-param-sfo — PS5 param.json info tool (Kyty-012)\n\n");
		std::printf("Usage: kytyps5-param-sfo <param.json>\n");
		std::printf("\nReads a PS5 param.json file and prints all key-value pairs.\n");
		return 1;
	}

	std::ifstream file(argv[1]);
	if (!file) {
		std::fprintf(stderr, "Error: cannot open %s\n", argv[1]);
		return 1;
	}

	std::stringstream ss;
	ss << file.rdbuf();
	std::string content = ss.str();

	json root;
	try {
		root = json::parse(content);
	} catch (const json::parse_error& e) {
		std::fprintf(stderr, "Error: invalid JSON: %s\n", e.what());
		return 1;
	}

	if (!root.is_object()) {
		std::fprintf(stderr, "Error: expected JSON object at root\n");
		return 1;
	}

	std::printf("=== param.json contents ===\n");
	std::printf("File: %s\n\n", argv[1]);

	for (auto it = root.begin(); it != root.end(); ++it) {
		const auto& key = it.key();
		const auto& value = it.value();

		if (value.is_string()) {
			std::printf("  %-30s = %s\n", key.c_str(), value.get<std::string>().c_str());
		} else if (value.is_number_integer()) {
			std::printf("  %-30s = %lld\n", key.c_str(), static_cast<long long>(value.get<int64_t>()));
		} else if (value.is_number_unsigned()) {
			std::printf("  %-30s = %llu\n", key.c_str(), static_cast<unsigned long long>(value.get<uint64_t>()));
		} else if (value.is_boolean()) {
			std::printf("  %-30s = %s\n", key.c_str(), value.get<bool>() ? "true" : "false");
		} else if (value.is_array()) {
			std::printf("  %-30s = [array, %zu elements]\n", key.c_str(), value.size());
		} else if (value.is_object()) {
			std::printf("  %-30s = [object, %zu keys]\n", key.c_str(), value.size());
		} else {
			std::printf("  %-30s = [unknown type]\n", key.c_str());
		}
	}

	std::printf("\nTotal keys: %zu\n", root.size());
	return 0;
}
