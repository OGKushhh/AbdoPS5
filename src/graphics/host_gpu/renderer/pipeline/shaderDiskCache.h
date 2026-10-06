#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_SHADERDISKCACHE_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_SHADERDISKCACHE_H_

#include "common/common.h"
#include "graphics/shader/recompiler/ShaderRecompiler.h"
#include "graphics/shader/recompiler/ir/ResourceSnapshot.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"
#include "graphics/shader/shader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Libs::Graphics::ShaderDiskCache {

// Bump when any serialized structure below changes shape or semantics.
inline constexpr uint64_t Magic        = 0x4b59545953484301ull; // "KYTYSHC" 0x01
inline constexpr uint32_t FormatVersion = 3; // v3: IndirectDescriptor selector + table_immediate/record_bytes/scalar

// Bounds so a corrupted or hostile file cannot blow up allocations.
inline constexpr uint32_t MaxVectorElements = 1u << 20;
inline constexpr uint32_t MaxStringLength   = 4096;
inline constexpr uint64_t MaxFileSize       = 1ull << 30; // 1 GiB

class Reader {
public:
		Reader() = default;
		explicit Reader(std::span<const uint8_t> bytes): m_data(bytes.data()), m_size(bytes.size()) {}

		[[nodiscard]] bool Ok() const {
				return m_ok;
		}
		[[nodiscard]] size_t Remaining() const {
				return m_ok ? m_size - m_pos : 0;
		}

		bool   Read(void* dst, size_t size);
		uint8_t  ReadU8();
		uint32_t ReadU32();
		uint64_t ReadU64();
		bool     ReadBool();
		std::string ReadString();
		std::vector<uint32_t> ReadWords();
		std::vector<uint8_t>  ReadBytes();

		template <typename E>
		E ReadEnum() {
				return static_cast<E>(ReadU32());
		}

private:
		const uint8_t* m_data = nullptr;
		size_t         m_size = 0;
		size_t         m_pos  = 0;
		bool           m_ok   = true;
};

class Writer {
public:
		void Write(const void* data, size_t size);
		void WriteU8(uint8_t value);
		void WriteU32(uint32_t value);
		void WriteU64(uint64_t value);
		void WriteBool(bool value);
		void WriteString(std::string_view value);
		void WriteWords(std::span<const uint32_t> value);
		void WriteBytes(std::span<const uint8_t> value);

		template <typename E>
		void WriteEnum(E value) {
				WriteU32(static_cast<uint32_t>(value));
		}

		[[nodiscard]] size_t Size() const {
				return m_data.size();
		}
		[[nodiscard]] const std::vector<uint8_t>& Data() const {
				return m_data;
		}
		// Hands the buffer over; the writer is empty afterwards.
		[[nodiscard]] std::vector<uint8_t> TakeData() {
				return std::move(m_data);
		}

private:
		std::vector<uint8_t> m_data;
};

// Wire key: identical shape to the program cache key (stage, guest shader hash,
// user data word count, code size, static-state words).
struct Key {
		ShaderType            stage           = ShaderType::Unknown;
		uint64_t              hash            = 0;
		uint32_t              user_data_count = 0;
		uint32_t              code_size       = 0;
		std::vector<uint32_t> static_state;

		bool operator==(const Key&) const = default;
};

struct KeyHash {
		std::size_t operator()(const Key& key) const;
};

struct LoadedPermutation {
		ShaderRecompiler::IR::ResourceSpecialization specialization;
		ShaderRecompiler::IR::CompiledShaderInfo     program;
		std::vector<uint32_t>                        spirv;
};

struct LoadedProgram {
		ShaderRecompiler::IR::ResourcePlan plan;
		bool                               skip_dispatch = false;
		std::vector<LoadedPermutation>     permutations;
};

// Index over an on-disk shader cache. The whole file stays in memory; entries
// are deserialized lazily through bounds-checked readers positioned at bodies.
class ShaderDiskIndex {
public:
		// A missing file is a valid empty cache; false is returned only when the
		// file exists but cannot be trusted (bad magic/version/signature/offsets).
		bool Open(const std::filesystem::path& path, std::string_view signature);

		bool Lookup(const Key& key, Reader& body);

		// Visits every indexed entry with its raw body bytes.
		void ForEach(const std::function<void(const Key&, std::span<const uint8_t>)>& fn) const;

		[[nodiscard]] size_t Size() const {
				return m_entries.size();
		}

private:
		std::unordered_map<Key, std::pair<size_t, size_t>, KeyHash> m_entries;
		std::vector<uint8_t>                                        m_bytes;
};

// Structure serialization. All readers fail closed: any malformed input makes
// the whole entry unusable and the caller falls back to retranslation.
bool WriteResourcePlan(Writer& w, const ShaderRecompiler::IR::ResourcePlan& plan);
std::optional<ShaderRecompiler::IR::ResourcePlan> ReadResourcePlan(Reader& r);

bool WriteCompiledShaderInfo(Writer& w, const ShaderRecompiler::IR::CompiledShaderInfo& info);
std::optional<ShaderRecompiler::IR::CompiledShaderInfo> ReadCompiledShaderInfo(Reader& r);

bool WriteResourceSpecialization(Writer& w,
								 const ShaderRecompiler::IR::ResourceSpecialization& spec);
std::optional<ShaderRecompiler::IR::ResourceSpecialization> ReadResourceSpecialization(Reader& r);

std::optional<LoadedProgram> ReadProgramBody(Reader& r);

} // namespace Libs::Graphics::ShaderDiskCache

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_SHADERDISKCACHE_H_
