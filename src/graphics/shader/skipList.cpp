#include "graphics/shader/skipList.h"

#include "common/file.h"
#include "common/logging/log.h"
#include "common/stringUtils.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

namespace Libs::Graphics {

// Make the static members accessible to the LoadJsonSkipList helper.
// (They're private in the header for encapsulation, but the loader needs access.)
struct ShaderSkipListAccess {
	static std::unordered_set<uint64_t>& Hashes() { return ShaderSkipList::s_skip_hashes; }
	static bool& SkipAllCompute() { return ShaderSkipList::s_skip_all_compute; }
	static bool& SkipAllGraphics() { return ShaderSkipList::s_skip_all_graphics; }
};

std::unordered_set<uint64_t> ShaderSkipList::s_skip_hashes;
bool                         ShaderSkipList::s_skip_all_compute  = false;
bool                         ShaderSkipList::s_skip_all_graphics  = false;
std::filesystem::path        ShaderSkipList::s_loaded_path;
bool                         ShaderSkipList::s_initialized         = false;

using Json = nlohmann::json;

static bool LoadJsonSkipList(const std::filesystem::path& path, uint64_t* count) {
	if (!Common::File::IsFileExisting(path)) {
		return false;
	}

	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}

	const auto root = Json::parse(file, nullptr, false);
	if (!root.is_object()) {
		LOGF("ShaderSkipList: invalid JSON in %s\n", Common::PathToString(path).c_str());
		return false;
	}

	// Load skip hashes
	if (root.contains("skips") && root["skips"].is_array()) {
		for (const auto& hash_str : root["skips"]) {
			if (!hash_str.is_string()) {
				continue;
			}
			const auto str = hash_str.get<std::string>();
			// Parse hex string "0x..." to uint64_t
			uint64_t hash = 0;
			if (str.starts_with("0x") || str.starts_with("0X")) {
				hash = std::stoull(str.substr(2), nullptr, 16);
			} else {
				hash = std::stoull(str, nullptr, 16);
			}
			ShaderSkipListAccess::Hashes().insert(hash);
			(*count)++;
		}
	}

	// Load skip-all flags
	if (root.contains("skip_all_compute") && root["skip_all_compute"].is_boolean()) {
		ShaderSkipListAccess::SkipAllCompute() = root["skip_all_compute"].get<bool>();
	}
	if (root.contains("skip_all_graphics") && root["skip_all_graphics"].is_boolean()) {
		ShaderSkipListAccess::SkipAllGraphics() = root["skip_all_graphics"].get<bool>();
	}

	return true;
}

void ShaderSkipList::Init(const std::string& title_id) {
	s_initialized = true;
	s_skip_hashes.clear();
	s_skip_all_compute = false;
	s_skip_all_graphics = false;

	uint64_t count = 0;

	// 1. Load global skip list (data/shader_skips.json)
	const auto global_path = std::filesystem::path("data") / "shader_skips.json";
	if (LoadJsonSkipList(global_path, &count)) {
		s_loaded_path = global_path;
		LOGF("ShaderSkipList: loaded %llu hashes from %s\n",
		     count, Common::PathToString(global_path).c_str());
	}

	// 2. Load per-game skip list (data/shader_skips/<title_id>.json)
	uint64_t game_count = 0;
	const auto game_path = std::filesystem::path("data") / "shader_skips" / (title_id + ".json");
	if (LoadJsonSkipList(game_path, &game_count)) {
		s_loaded_path = game_path;
		LOGF("ShaderSkipList: loaded %llu additional hashes from %s\n",
		     game_count, Common::PathToString(game_path).c_str());
	}

	const auto total = s_skip_hashes.size();
	if (total > 0 || s_skip_all_compute || s_skip_all_graphics) {
		LOGF("ShaderSkipList: %s — %zu hashes, skip_all_compute=%d, skip_all_graphics=%d\n",
		     title_id.c_str(), total, s_skip_all_compute, s_skip_all_graphics);
	}
}

bool ShaderSkipList::ShouldSkip(uint64_t hash) {
	if (!s_initialized) {
		return false;
	}
	return s_skip_hashes.find(hash) != s_skip_hashes.end();
}

void ShaderSkipList::AddSkip(uint64_t hash) {
	s_skip_hashes.insert(hash);
	LOGF("ShaderSkipList: added hash 0x%016llX (total: %zu)\n", hash, s_skip_hashes.size());
}

bool ShaderSkipList::Save() {
	if (s_loaded_path.empty()) {
		return false;
	}

	Json root;
	Json skips = Json::array();
	for (const auto hash : s_skip_hashes) {
		char buf[32];
		std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(hash));
		skips.push_back(buf);
	}
	root["skips"] = skips;
	root["skip_all_compute"] = s_skip_all_compute;
	root["skip_all_graphics"] = s_skip_all_graphics;

	std::ofstream file(s_loaded_path, std::ios::binary);
	if (!file) {
		LOGF("ShaderSkipList: cannot write to %s\n", Common::PathToString(s_loaded_path).c_str());
		return false;
	}

	file << root.dump(2);
	LOGF("ShaderSkipList: saved %zu hashes to %s\n",
	     s_skip_hashes.size(), Common::PathToString(s_loaded_path).c_str());
	return true;
}

} // namespace Libs::Graphics
