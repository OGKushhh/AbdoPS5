#include "graphics/host_gpu/renderer/pipeline/pipelineCache.h"

#include "graphics/host_gpu/renderer/pipeline/pipelineLibrary.h"

#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "common/file.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "common/threads.h"
#include "graphics/guest_gpu/hardwareContext.h"
#include "graphics/host_gpu/renderer/colorRenderTarget.h"
#include "graphics/host_gpu/renderer/debug.h"
#include "graphics/host_gpu/renderer/depthRenderTarget.h"
#include "graphics/host_gpu/renderer/image/imageView.h"
#include "graphics/host_gpu/renderer/pipeline/blendMapping.h"
#include "graphics/host_gpu/renderer/pipeline/shaderDiskCache.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/shader/recompiler/ShaderRecompiler.h"
#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"
#include "graphics/shader/shaderCompiler.h"
#include "kernel/memory.h"
#include "kytyGitVersion.h"
#include "loader/systemContent.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <future>
#include <limits>
#include <magic_enum.hpp>
#include <optional>
#include <span>
#include <spirv-tools/libspirv.hpp>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <xxhash.h>

namespace Libs::Graphics {

namespace {

vk::PolygonMode ResolvePolygonMode(const HW::ModeControl& mode, bool cull_front, bool cull_back) {
	// CxPrimitiveSetup::PolygonMode disables both per-face modes when it is zero.
	if (mode.poly_mode == 0) {
		return vk::PolygonMode::eFill;
	}
	EXIT_NOT_IMPLEMENTED(mode.poly_mode != 1);
	if (cull_front && cull_back) {
		return vk::PolygonMode::eFill;
	}
	if (!cull_front && !cull_back && mode.polymode_front_ptype != mode.polymode_back_ptype) {
		EXIT("Pipeline: different polygon modes for two visible faces are unsupported\n");
	}
	// Vulkan has one polygon mode. A culled face does not constrain that mode.
	const auto polygon_mode = cull_front ? mode.polymode_back_ptype : mode.polymode_front_ptype;
	switch (polygon_mode) {
		case 0: return vk::PolygonMode::ePoint;
		case 1: return vk::PolygonMode::eLine;
		case 2: return vk::PolygonMode::eFill;
		default: EXIT("Pipeline: invalid polygon mode %u\n", polygon_mode);
	}
}

std::string DriverCacheSignature(const vk::PhysicalDeviceProperties& properties) {
	constexpr char hex[] = "0123456789abcdef";
	std::string    uuid(VK_UUID_SIZE * 2, '0');
	for (size_t i = 0; i < VK_UUID_SIZE; i++) {
		uuid[i * 2]     = hex[properties.pipelineCacheUUID[i] >> 4u];
		uuid[i * 2 + 1] = hex[properties.pipelineCacheUUID[i] & 0xfu];
	}
	return fmt::format("KytyPC1:{}:{:08x}:{:08x}:{:08x}:{}\n", KYTY_GIT_REVISION,
	                   properties.vendorID, properties.deviceID, properties.driverVersion, uuid);
}

std::string PipelineCacheTitleId() {
	std::string title_id;
	if ((!Loader::SystemContentParamSfoGetString("TITLE_ID", &title_id) || title_id.empty()) &&
	    (!Loader::SystemContentParamSfoGetString("CONTENT_ID", &title_id) || title_id.empty())) {
		return {};
	}
	if (!std::ranges::all_of(title_id, [](unsigned char c) {
		    return std::isalnum(c) != 0 || c == '-' || c == '_';
	    })) {
		return {};
	}
	return title_id;
}

template <typename... Args>
void PipelineCacheLog(fmt::format_string<Args...> format, Args&&... args) {
	auto message = fmt::format(format, std::forward<Args>(args)...);
	message += '\n';
	Log::WriteToConsoleAndLog(message);
}

bool ReadShaderGuestMemory(void*, uint64_t address, std::span<uint32_t> values) {
	return !values.empty() &&
	       Libs::LibKernel::Memory::TryReadGpuCleanBacking(address, values.data(), values.size_bytes());
}

void DumpShaderSpirv(const char* stage_name, uint64_t shader_hash,
                     const std::vector<uint32_t>& spirv) {
	if (!Config::GraphicsDebugDumpEnabled()) {
		return;
	}
	static std::atomic_int id = 0;
	const auto path = Config::GetShaderLogFolder() / fmt::format("{:04d}_new_shader_{}_{:016x}.spv",
	                                                             id++, stage_name, shader_hash);
	Common::File::CreateDirectories(path.parent_path());
	Common::File file(path);
	if (file.IsInvalid()) {
		const auto path_text = Common::PathToString(path);
		LOGF_COLOR(Log::Color::BrightRed, "Can't create file: %s\n", path_text.c_str());
		return;
	}
	file.Write(spirv.data(), spirv.size() * sizeof(uint32_t));
}

void DumpShaderOriginal(const char* stage_name, uint64_t shader_hash,
                        std::span<const uint32_t> code) {
	if (!Config::GraphicsDebugDumpEnabled()) {
		return;
	}
	EXIT_IF(code.empty());
	static std::atomic_int id = 0;
	const auto path = Config::GetShaderLogFolder() / "original" /
	                  fmt::format("{:04d}_new_shader_{}_{:016x}.bin", id++, stage_name, shader_hash);
	Common::File::CreateDirectories(path.parent_path());
	Common::File file(path);
	if (file.IsInvalid()) {
		const auto path_text = Common::PathToString(path);
		LOGF_COLOR(Log::Color::BrightRed, "Can't create file: %s\n", path_text.c_str());
		return;
	}
	file.Write(code.data(), code.size_bytes());
}

bool ValidateShaderSpirv(const char* label, uint64_t shader_hash,
                         const std::vector<uint32_t>& spirv) {
	if (!Config::ShaderValidationEnabled()) {
		return true;
	}
	spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_3);
	std::string          messages;
	tools.SetMessageConsumer([&messages](spv_message_level_t, const char*,
	                                     const spv_position_t& position, const char* message) {
		messages += fmt::format("{}: {} ({}) {}\n", static_cast<int>(position.line),
		                        static_cast<int>(position.column), static_cast<int>(position.index),
		                        message);
	});
	if (tools.Validate(spirv)) {
		return true;
	}
	spvtools::SpirvTools disassembler(SPV_ENV_VULKAN_1_2);
	std::string          text;
	disassembler.Disassemble(spirv, &text,
	                         static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_COMMENT) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_INDENT) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_COLOR));
	LOGF_COLOR(Log::Color::BrightRed, "%s SPIR-V validation failed hash=0x%016" PRIx64 ":\n%s",
	           label, shader_hash, messages.c_str());
	LOGF("%s\n", text.c_str());
	return false;
}

} // namespace

std::size_t PipelineCache::GraphicsPipelineKeyHash::operator()(const GraphicsPipelineKey& key) const {
	std::size_t hash = 0;
	PipelineKeyHash::Mix(hash, key.rendering.color_count);
	for (uint32_t i = 0; i < key.rendering.color_count; i++) {
		PipelineKeyHash::Mix(hash, static_cast<uint32_t>(key.rendering.color_formats[i]));
	}
	PipelineKeyHash::Mix(hash, static_cast<uint32_t>(key.rendering.depth_format));
	PipelineKeyHash::Mix(hash, static_cast<uint32_t>(key.rendering.stencil_format));
	for (const auto id: key.vertex_shader_ids) {
		PipelineKeyHash::Mix(hash, id);
	}
	PipelineKeyHash::Mix(hash, key.ps_shader_id);
	PipelineKeyHash::Mix(hash, key.vertex_input.binding_count);
	for (uint32_t i = 0; i < key.vertex_input.binding_count; i++) {
		PipelineKeyHash::Mix(hash, key.vertex_input.bindings[i].stride);
		PipelineKeyHash::Mix(hash, key.vertex_input.bindings[i].instance);
	}
	PipelineKeyHash::Mix(hash, key.vertex_input.attribute_count);
	for (uint32_t i = 0; i < key.vertex_input.attribute_count; i++) {
		PipelineKeyHash::Mix(hash, key.vertex_input.attributes[i].offset);
		PipelineKeyHash::Mix(hash, key.vertex_input.attributes[i].binding);
	}
	PipelineKeyHash::Mix(hash, XXH3_64bits(&key.static_params, sizeof(key.static_params)));
	return hash;
}

struct PipelineCache::ProgramCache {
	struct ProgramKey {
		ShaderType            stage           = ShaderType::Unknown;
		uint64_t              hash            = 0;
		uint32_t              user_data_count = 0;
		uint32_t              code_size       = 0;
		std::vector<uint32_t> static_state;

		bool operator==(const ProgramKey&) const = default;
	};

	struct Permutation {
		ShaderRecompiler::IR::ResourceSpecialization specialization;
		ShaderRecompiler::IR::CompiledShaderInfo     program;
		ShaderProgram                                handle;
		std::vector<uint32_t>                        spirv; // retained for the disk cache
	};

	// A permutation's shader module and info, compiled on any thread.
	struct CompiledModule {
		ShaderRecompiler::IR::CompiledShaderInfo program;
		vk::ShaderModule                         module = nullptr;
		std::vector<uint32_t>                    spirv; // retained for the disk cache
	};

	// A permutation compiling on a worker thread.
	struct PendingPermutation {
		ShaderRecompiler::IR::ResourceSpecialization specialization;
		uint32_t                                     push_data_cursor = 0;
		std::future<CompiledModule>                  compiled;
	};

	struct SourceEntry {
		explicit SourceEntry(ShaderRecompiler::IR::ResourcePlan plan)
		    : resource_plan(std::move(plan)) {
			permutations.reserve(8);
		}

		ShaderRecompiler::IR::ResourcePlan           resource_plan;
		ShaderRecompiler::IR::ResourceSnapshot       resources;
		ShaderRecompiler::IR::ResourceSpecialization specialization;
		std::vector<Permutation>                    permutations;
		// Permutations compiling on worker threads, picked up by a later lookup.
		std::vector<PendingPermutation>             pending;
		bool                                        skip_dispatch = false;
	};

	struct ProgramKeyHash {
		std::size_t operator()(const ProgramKey& key) const {
			std::size_t hash = static_cast<std::size_t>(key.stage);
			PipelineKeyHash::Mix(hash, static_cast<std::size_t>(key.hash));
			if constexpr (sizeof(std::size_t) < sizeof(uint64_t)) {
				PipelineKeyHash::Mix(hash, static_cast<std::size_t>(key.hash >> 32u));
			}
			PipelineKeyHash::Mix(hash, key.user_data_count);
			PipelineKeyHash::Mix(hash, key.code_size);
			PipelineKeyHash::Mix(hash, key.static_state.size());
			// Bucket same-shape static variants by source. ProgramKey equality performs the one
			// exact state comparison needed on a stable hit without hashing the full state first.
			return hash;
		}
	};

	static constexpr std::size_t MaxStaticKeyWords = 32 + ShaderVertexInputInfo::RES_MAX * 6;

	static const char* StageName(ShaderType stage) {
		switch (stage) {
			case ShaderType::Vertex: return "vs";
			case ShaderType::Mesh: return "ms";
			case ShaderType::Local: return "ls";
			case ShaderType::TessellationControl: return "hs";
			case ShaderType::TessellationEvaluation: return "ds";
			case ShaderType::Pixel: return "ps";
			case ShaderType::Compute: return "cs";
			default: EXIT("invalid pipeline shader stage\n");
		}
		return nullptr;
	}

	// Compiles a translated permutation to its SPIR-V module. Reads only its arguments, so
	// it may run on a worker thread.
	static CompiledModule
	CompileModule(vk::Device                                     device,
	              const ShaderRecompiler::CompileOptions&      options,
	              ShaderRecompiler::TranslateResult            translated,
	              const ShaderRecompiler::IR::ResourceSpecialization& specialization,
	              uint32_t push_data_start_dword) {
		const char* stage_name = StageName(options.stage);
		auto result = ShaderRecompiler::CompileProgram(std::move(translated), options,
		                                               specialization, push_data_start_dword);
		if (!ValidateShaderSpirv(options.dump_label, options.shader_hash, result.spirv)) {
			DumpShaderSpirv(stage_name, options.shader_hash, result.spirv);
			EXIT("%s failed hash=0x%016" PRIx64 ": SPIR-V validation failed\n", options.dump_label,
			     options.shader_hash);
		}
		DumpShaderSpirv(stage_name, options.shader_hash, result.spirv);

		const auto module = CompileSPV(result.spirv, device);
		EXIT_IF(module == nullptr);
		return {
		    .program = std::move(result.program).TakeCompiledInfo(),
		    .module  = module,
		    .spirv   = std::move(result.spirv),
		};
	}

	// Numbers a compiled permutation.
	Permutation MakePermutation(const ShaderRecompiler::CompileOptions&      options,
	                            ShaderRecompiler::IR::ResourceSpecialization specialization,
	                            CompiledModule                               compiled) {
		if (options.dump_ir) {
			LOGF("%s SPIR-V words=%" PRIu64 " wave_size=%u\n", options.dump_label,
			     static_cast<uint64_t>(compiled.spirv.size()), options.wave_size);
		}
		return {
		    .specialization = std::move(specialization),
		    .program        = std::move(compiled.program),
		    .handle         = {.id = ++next_shader_id, .module = compiled.module},
		    .spirv          = std::move(compiled.spirv),
		};
	}

	Permutation CompilePermutation(const ShaderRecompiler::CompileOptions&      options,
	                               ShaderRecompiler::TranslateResult            translated,
	                               ShaderRecompiler::IR::ResourceSpecialization specialization,
	                               uint32_t push_data_start_dword) {
		auto compiled = CompileModule(device, options, std::move(translated), specialization,
		                              push_data_start_dword);
		return MakePermutation(options, std::move(specialization), std::move(compiled));
	}

	struct StageOptions {
		ShaderRecompiler::CompileOptions options;
		const char*                       stage_name = nullptr;
	};

	// Shared option builder so worker-prefetched translations see exactly the same
	// inputs the GPU thread would pass when translating inline.
	template <typename InputInfo>
	static StageOptions MakeStageOptions(const ShaderParams& params, InputInfo& input_info) {
		ShaderType stage;
		if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
			stage = input_info.logical_stage;
		} else if constexpr (std::is_same_v<InputInfo, ShaderPixelInputInfo>) {
			stage = ShaderType::Pixel;
		} else {
			static_assert(std::is_same_v<InputInfo, ShaderComputeInputInfo>);
			stage = ShaderType::Compute;
		}

		ShaderStageInputInfo stage_input {};
		if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
			stage_input.vertex = &input_info;
		} else if constexpr (std::is_same_v<InputInfo, ShaderPixelInputInfo>) {
			stage_input.pixel = &input_info;
		} else {
			stage_input.compute = &input_info;
		}
		const char* label      = nullptr;
		const char* stage_name = nullptr;
		switch (stage) {
			case ShaderType::Vertex: label = "ShaderRecompiler VS"; stage_name = "vs"; break;
			case ShaderType::Mesh: label = "ShaderRecompiler MS"; stage_name = "ms"; break;
			case ShaderType::Local: label = "ShaderRecompiler LS"; stage_name = "ls"; break;
			case ShaderType::TessellationControl: label = "ShaderRecompiler HS"; stage_name = "hs"; break;
			case ShaderType::TessellationEvaluation: label = "ShaderRecompiler DS"; stage_name = "ds"; break;
			case ShaderType::Pixel: label = "ShaderRecompiler PS"; stage_name = "ps"; break;
			case ShaderType::Compute: label = "ShaderRecompiler CS"; stage_name = "cs"; break;
			default: EXIT("invalid pipeline shader stage\n");
		}
		ShaderRecompiler::CompileOptions options;
		options.stage       = stage;
		options.shader_hash = params.hash;
		options.user_data   = std::span(params.user_data).first(params.user_data_count);
		options.back_code   = params.back_code;
		options.dump_ir     = Config::GetShaderLogDirection() != Config::LogDirection::Silent;
		options.early_dump  = options.dump_ir;
		options.dump_label  = label;
		options.input_info  = stage_input;

		if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
			options.user_data_base = 8;
			options.wave_size = input_info.wave_size;
			if (stage == ShaderType::Mesh || stage == ShaderType::TessellationControl) {
				options.user_data_base = 0;
				options.wave_size = stage == ShaderType::Mesh ? input_info.mesh.wave_size : 64u;
			}
		} else {
			options.wave_size = input_info.wave_size;
		}
		return {std::move(options), stage_name};
	}

	// A background translation's own copies of its inputs; `options` points into them, so
	// the job stays where it was allocated.
	template <typename InputInfo>
	struct TranslationInput {
		std::vector<uint32_t>            code;
		std::vector<uint32_t>            back_code;
		std::array<uint32_t, 40>         user_data {};
		InputInfo                        input_info;
		ShaderRecompiler::CompileOptions options;
	};

	template <typename InputInfo>
	[[nodiscard]] static std::unique_ptr<TranslationInput<InputInfo>>
	CopyTranslationInput(ShaderType stage, const ShaderParams& params, const InputInfo& input_info) {
		auto input      = std::make_unique<TranslationInput<InputInfo>>();
		input->code.assign(params.code.begin(), params.code.end());
		input->back_code.assign(params.back_code.begin(), params.back_code.end());
		input->user_data  = params.user_data;
		input->input_info = input_info;
		input->input_info.stage = {};
		ShaderParams copy;
		copy.code            = input->code;
		copy.user_data       = input->user_data;
		copy.user_data_count = params.user_data_count;
		copy.hash            = params.hash;
		copy.back_code       = input->back_code;
		input->options       = MakeStageOptions(copy, input->input_info).options;
		// Worker threads never write the shader log.
		input->options.dump_ir    = false;
		input->options.early_dump = false;
		return input;
	}

	// Posts a translation or module compile to the worker threads, counted in `jobs` until
	// its result is set.
	template <typename Job>
	void PostJob(Job&& job, bool urgent) {
		jobs->in_flight.fetch_add(1, std::memory_order_relaxed);
		const bool posted = workers->Post(
		    [counts = jobs, job = std::forward<Job>(job)]() mutable {
			    job();
			    counts->finished.fetch_add(1, std::memory_order_relaxed);
			    counts->in_flight.fetch_sub(1, std::memory_order_release);
		    },
		    urgent);
		if (!posted) {
			jobs->in_flight.fetch_sub(1, std::memory_order_relaxed);
		}
	}

	template <typename T>
	[[nodiscard]] static bool IsReady(const std::future<T>& future) {
		return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	// Null when the job was dropped (the worker threads stopped) or failed.
	template <typename T>
	[[nodiscard]] static std::optional<T> TakeResult(std::future<T>& future) {
		try {
			return future.get();
		} catch (const std::exception&) {
			return std::nullopt;
		}
	}

	// Whether the shader stores to buffers, writes images or uses GDS, found by decoding only.
	// Buffer atomics do not count (see StoresData in renderDraw.cpp).
	bool StoresData(ShaderType stage, const ShaderParams& params) {
		if (const auto known = stores_data.find(params.hash); known != stores_data.end()) {
			return known->second;
		}
		namespace Decoder = ShaderRecompiler::Decoder;
		const auto stores = [](const Decoder::Program& program) {
			return std::ranges::any_of(program.instructions, [](const Decoder::Instruction& inst) {
				if (inst.family == Decoder::Family::DS && inst.gds) {
					return true;
				}
				const auto name = magic_enum::enum_name(inst.opcode);
				return name.starts_with("BUFFER_STORE") || name.starts_with("TBUFFER_STORE") ||
				       name.starts_with("FLAT_STORE") || name.starts_with("GLOBAL_STORE") ||
				       name.starts_with("IMAGE_STORE") || name.starts_with("IMAGE_ATOMIC");
			});
		};
		// Decoded as TranslateProgram decodes it.
		Decoder::Program front;
		if (!params.back_code.empty() || stage == ShaderType::Local) {
			front = Decoder::DecodeFrontProgram(params.code);
		} else {
			Decoder::DecodeProgram(params.code, front);
		}
		bool result = front.has_bvh || stores(front);
		if (!result && !params.back_code.empty()) {
			Decoder::Program back;
			Decoder::DecodeProgram(params.back_code, back);
			result = back.has_bvh || stores(back);
		}
		stores_data.emplace(params.hash, result);
		return result;
	}

	// Starts translating a source nothing has translated or queued yet: the vertex stages of a
	// draw before its pixel stage compiles on this thread, or a stage a look-ahead predicts whose
	// push data position is unknown until an earlier stage compiles.
	template <typename InputInfo>
	void QueueSourceTranslation(const ShaderParams& params, const InputInfo& input_info,
	                          ShaderType stage) {
		if (workers == nullptr ||
		    Config::GetShaderLogDirection() != Config::LogDirection::Silent) {
			return;
		}
		lookup_key.stage           = stage;
		lookup_key.hash            = params.hash;
		lookup_key.user_data_count = params.user_data_count;
		lookup_key.code_size       = static_cast<uint32_t>(params.code.size());
		BuildStageStaticKey(input_info, lookup_key.static_state);
		if (programs.contains(lookup_key) || pending_sources.contains(lookup_key)) {
			return;
		}
		// A program the disk cache can restore needs no prefetched translation, and a queued
		// job no lookup would consume would leak its slot.
		if (m_disk != nullptr) {
			ShaderDiskCache::Reader body;
			if (m_disk->Lookup(ToDiskKey(lookup_key), body)) {
				return;
			}
		}
		std::promise<ShaderRecompiler::TranslateResult> promise;
		pending_sources.emplace(lookup_key, promise.get_future());
		PostJob(
		    [input = CopyTranslationInput(stage, params, input_info),
		     promise = std::move(promise)]() mutable {
			    promise.set_value(ShaderRecompiler::TranslateProgram(input->code, input->options));
		    },
		    false);
	}



	template <typename InputInfo>
	// With ProgramWait::Defer (a draw, with asynchronous pipelines) or ProgramWait::Prefetch
	// (a look-ahead prediction), a permutation not compiled yet is translated and compiled on
	// the worker threads, and the result is empty with `*pending` set; the same lookup later
	// picks it up. Defer jobs go ahead of queued prefetches.
	ShaderProgram Get(const ShaderParams& params, InputInfo& input_info,
	                  uint32_t& push_data_cursor, ProgramWait wait = ProgramWait::Wait,
	                  bool* pending = nullptr) {
		ShaderType stage;
		if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
			stage = input_info.logical_stage;
		} else if constexpr (std::is_same_v<InputInfo, ShaderPixelInputInfo>) {
			stage = ShaderType::Pixel;
		} else {
			static_assert(std::is_same_v<InputInfo, ShaderComputeInputInfo>);
			stage = ShaderType::Compute;
		}
		const bool background = wait != ProgramWait::Wait && workers != nullptr &&
		                        Config::GetShaderLogDirection() == Config::LogDirection::Silent;
		const bool urgent     = wait == ProgramWait::Defer;

		const auto user_data = std::span(params.user_data).first(params.user_data_count);
		lookup_key.stage           = stage;
		lookup_key.hash            = params.hash;
		lookup_key.user_data_count = params.user_data_count;
		lookup_key.code_size       = static_cast<uint32_t>(params.code.size());
		BuildStageStaticKey(input_info, lookup_key.static_state);
		auto                                         entry = programs.find(lookup_key);
		if (entry == programs.end()) {
			entry = TryLoadProgramFromDisk(lookup_key);
		}
		if (entry != programs.end() && entry->second.skip_dispatch) {
			return {};
		}
		const ShaderRecompiler::IR::SrtRuntime       runtime {
		    .user_data                  = user_data,
		    .shader_base                = params.Base(),
		    .read_specialization_memory = ReadShaderGuestMemory,
		};
		if (entry != programs.end()) {
			EXIT_IF(!ShaderRecompiler::IR::MaterializeResources(
			    entry->second.resource_plan, runtime, entry->second.resources,
			    entry->second.specialization));
			if (const auto permutation = std::ranges::find_if(
			        entry->second.permutations, [&](const Permutation& candidate) {
				        const auto& layout = candidate.program.bindings;
				        return layout.push_data_start_dword ==
				                   ShaderRecompiler::IR::PushData::StartFor(
				                       push_data_cursor, layout.ShaderDataDwords()) &&
				               candidate.specialization == entry->second.specialization;
			        });
			    permutation != entry->second.permutations.end()) {
				input_info.stage = {.program   = &permutation->program,
				                    .resources = &entry->second.resources};
				permutation->program.bindings.AdvancePushData(push_data_cursor);
				return permutation->handle;
			}
		}

		const auto defer = [&] {
			if (pending != nullptr) {
				*pending = true;
			}
			return ShaderProgram {};
		};
		const auto  stage_options = MakeStageOptions(params, input_info);
		const auto& options       = stage_options.options;
		const char* stage_name    = stage_options.stage_name;
		std::optional<ShaderRecompiler::TranslateResult> translated;
		if (entry == programs.end()) {
			// A new source: translate it first.
			if (auto queued = pending_sources.find(lookup_key); queued != pending_sources.end()) {
				if (background && !IsReady(queued->second)) {
					return defer();
				}
				translated = TakeResult(queued->second);
				pending_sources.erase(queued);
			} else if (background) {
				std::promise<ShaderRecompiler::TranslateResult> promise;
				pending_sources.emplace(lookup_key, promise.get_future());
				PostJob(
				    [input   = CopyTranslationInput(stage, params, input_info),
				     promise = std::move(promise)]() mutable {
					    promise.set_value(
					        ShaderRecompiler::TranslateProgram(input->code, input->options));
				    },
				    urgent);
				return defer();
			}
			if (!translated) {
				DumpShaderOriginal(stage_name, options.shader_hash, params.code);
				translated = ShaderRecompiler::TranslateProgram(params.code, options);
			}
			if (translated->skip_dispatch) {
				entry = programs.try_emplace(lookup_key, ShaderRecompiler::IR::ResourcePlan {}).first;
				entry->second.skip_dispatch = true;
				return {};
			}
			entry = programs.try_emplace(lookup_key,
			    ShaderRecompiler::IR::ExtractResourcePlan(translated->program)).first;
			EXIT_IF(!ShaderRecompiler::IR::MaterializeResources(
			    entry->second.resource_plan, runtime, entry->second.resources,
			    entry->second.specialization));
		}
		auto& source = entry->second;

		// Then compile the permutation for this specialization and push data position.
		std::optional<CompiledModule> compiled;
		const auto queued_permutation = std::ranges::find_if(
		    source.pending, [&](const PendingPermutation& p) {
			    return p.push_data_cursor == push_data_cursor &&
			           p.specialization == source.specialization;
		    });
		if (queued_permutation != source.pending.end()) {
			if (background && !IsReady(queued_permutation->compiled)) {
				return defer();
			}
			compiled = TakeResult(queued_permutation->compiled);
			source.pending.erase(queued_permutation);
		} else if (background) {
			std::promise<CompiledModule> promise;
			source.pending.push_back({.specialization   = source.specialization,
			                          .push_data_cursor = push_data_cursor,
			                          .compiled         = promise.get_future()});
			PostJob(
			    [device = device, input = CopyTranslationInput(stage, params, input_info),
			     translated = std::move(translated), specialization = source.specialization,
			     push_data_cursor, promise = std::move(promise)]() mutable {
				    if (!translated) {
					    translated = ShaderRecompiler::TranslateProgram(input->code, input->options);
				    }
				    promise.set_value(CompileModule(device, input->options, std::move(*translated),
				                                    specialization, push_data_cursor));
			    },
			    urgent);
			return defer();
		}
		if (compiled) {
			source.permutations.push_back(
			    MakePermutation(options, source.specialization, std::move(*compiled)));
		} else {
			if (!translated) {
				DumpShaderOriginal(stage_name, options.shader_hash, params.code);
				translated = ShaderRecompiler::TranslateProgram(params.code, options);
			}
			source.permutations.push_back(CompilePermutation(
			    options, std::move(*translated), source.specialization, push_data_cursor));
		}
		const auto& permutation = source.permutations.back();
		input_info.stage = {.program = &permutation.program, .resources = &source.resources};
		permutation.program.bindings.AdvancePushData(push_data_cursor);

		std::array<size_t, static_cast<size_t>(ShaderType::TessellationEvaluation) + 1> counts {};
		for (const auto& [key, source]: programs) {
			counts[static_cast<size_t>(key.stage)] += source.permutations.size();
		}
		// Guest geometry shaders are compiled through the host mesh stage.
		std::printf("Shaders: VS %zu | PS %zu | CS %zu | GS %zu | LS %zu | HS %zu | TES %zu\n",
		            counts[static_cast<size_t>(ShaderType::Vertex)],
		            counts[static_cast<size_t>(ShaderType::Pixel)],
		            counts[static_cast<size_t>(ShaderType::Compute)],
		            counts[static_cast<size_t>(ShaderType::Mesh)],
		            counts[static_cast<size_t>(ShaderType::Local)],
		            counts[static_cast<size_t>(ShaderType::TessellationControl)],
		            counts[static_cast<size_t>(ShaderType::TessellationEvaluation)]);
		return permutation.handle;
	}

	static ShaderDiskCache::Key ToDiskKey(const ProgramKey& key) {
		ShaderDiskCache::Key disk_key;
		disk_key.stage           = key.stage;
		disk_key.hash            = key.hash;
		disk_key.user_data_count = key.user_data_count;
		disk_key.code_size       = key.code_size;
		disk_key.static_state    = key.static_state;
		return disk_key;
	}

	// Restores a program (plan + compiled permutations) from the disk cache. Any
	// malformed entry fails closed and the caller retranslates as before.
	std::unordered_map<ProgramKey, SourceEntry, ProgramKeyHash>::iterator
	TryLoadProgramFromDisk(const ProgramKey& key) {
		if (m_disk == nullptr) {
			return programs.end();
		}
		ShaderDiskCache::Reader body;
		if (!m_disk->Lookup(ToDiskKey(key), body)) {
			return programs.end();
		}
		auto loaded = ShaderDiskCache::ReadProgramBody(body);
		if (!loaded) {
			return programs.end();
		}
		auto entry = programs.try_emplace(key, std::move(loaded->plan)).first;
		entry->second.skip_dispatch = loaded->skip_dispatch;
		entry->second.permutations.reserve(loaded->permutations.size());
		for (auto& permutation: loaded->permutations) {
			vk::ShaderModuleCreateInfo create_info {};
			create_info.codeSize = permutation.spirv.size() * sizeof(uint32_t);
			create_info.pCode    = permutation.spirv.data();
			vk::ShaderModule     module = nullptr;
			if (device.createShaderModule(&create_info, nullptr, &module) !=
			        vk::Result::eSuccess ||
			    module == nullptr) {
				continue;
			}
			entry->second.permutations.push_back({std::move(permutation.specialization),
			                                    std::move(permutation.program),
			                                    ShaderProgram {++next_shader_id, module},
			                                    std::move(permutation.spirv)});
		}
		return entry;
	}

	void OpenDiskCache() {
		const auto title_id = PipelineCacheTitleId();
		if (title_id.empty()) {
			return;
		}
		if (std::getenv("KYTY_NO_SHADER_CACHE") != nullptr) {
			PipelineCacheLog("Shader disk cache: disabled (KYTY_NO_SHADER_CACHE)");
			return;
		}
		if (KYTY_BUILD != KYTY_BUILD_RELEASE) {
			PipelineCacheLog("Shader disk cache: disabled (non-Release build)");
			return;
		}
		const std::string_view git_hash     = KYTY_GIT_HASH;
		const std::string_view git_revision = KYTY_GIT_REVISION;
		if (git_hash == "unknown" || git_revision == "unknown") {
			PipelineCacheLog("Shader disk cache: disabled (unknown git revision)");
			return;
		}
		if (git_hash.ends_with("-dirty")) {
			PipelineCacheLog("Shader disk cache: disabled (dirty build)");
			return;
		}
		m_disk_path      = std::filesystem::path("_PipelineCache") / (title_id + ".shadercache");
		m_disk_signature = fmt::format("KytySC1:{}", git_revision);
		auto disk        = std::make_unique<ShaderDiskCache::ShaderDiskIndex>();
		if (disk->Open(m_disk_path, m_disk_signature)) {
			PipelineCacheLog("Shader disk cache: {} entrie(s), {}", disk->Size(),
			                 Common::PathToString(m_disk_path));
			m_disk = std::move(disk);
		} else {
			// Keep path and signature so the next save rewrites a fresh file.
			PipelineCacheLog("Shader disk cache: unusable, rebuilding ({})",
			                 Common::PathToString(m_disk_path));
		}
	}

	void SaveToDisk() {
		if (m_disk_path.empty()) {
			return;
		}
		struct StagedBody {
			ShaderDiskCache::Key key;
			std::vector<uint8_t> bytes;
		};
		std::vector<StagedBody> staged;
		std::unordered_map<ShaderDiskCache::Key, size_t, ShaderDiskCache::KeyHash> staged_index;
		if (m_disk != nullptr) {
			m_disk->ForEach([&](const ShaderDiskCache::Key& key, std::span<const uint8_t> body) {
				auto position = staged_index.emplace(key, staged.size());
				if (position.second) {
					staged.push_back({key, std::vector<uint8_t>(body.begin(), body.end())});
				}
			});
		}
		for (const auto& [key, entry]: programs) {
			ShaderDiskCache::Writer w;
			if (!ShaderDiskCache::WriteResourcePlan(w, entry.resource_plan)) {
				PipelineCacheLog("Shader disk cache: program {:#018x} skipped (serialize failed)",
				                 key.hash);
				continue;
			}
			w.WriteBool(entry.skip_dispatch);
			w.WriteU32(static_cast<uint32_t>(entry.permutations.size()));
			for (const auto& permutation: entry.permutations) {
				ShaderDiskCache::WriteResourceSpecialization(w, permutation.specialization);
				ShaderDiskCache::WriteCompiledShaderInfo(w, permutation.program);
				w.WriteWords(permutation.spirv);
			}
			auto disk_key = ToDiskKey(key);
			auto position = staged_index.find(disk_key);
			if (position != staged_index.end()) {
				staged[position->second] = {disk_key, w.TakeData()};
			} else {
				staged_index.emplace(disk_key, staged.size());
				staged.push_back({disk_key, w.TakeData()});
			}
		}

		ShaderDiskCache::Writer file;
		file.WriteU64(ShaderDiskCache::Magic);
		file.WriteU32(ShaderDiskCache::FormatVersion);
		file.WriteString(m_disk_signature);
		file.WriteU32(static_cast<uint32_t>(staged.size()));
		size_t cursor = 8 + 4 + 4 + m_disk_signature.size() + 4;
		for (const auto& body: staged) {
			cursor += 4 + 8 + 4 + 4 + 4 + 8 + 8 + body.key.static_state.size() * sizeof(uint32_t);
		}
		for (const auto& body: staged) {
			file.WriteU32(static_cast<uint32_t>(body.key.stage));
			file.WriteU64(body.key.hash);
			file.WriteU32(body.key.user_data_count);
			file.WriteU32(body.key.code_size);
			file.WriteWords(body.key.static_state);
			file.WriteU64(cursor);
			file.WriteU64(body.bytes.size());
			cursor += body.bytes.size();
		}
		for (const auto& body: staged) {
			file.Write(body.bytes.data(), body.bytes.size());
		}

		auto tmp = m_disk_path;
		tmp += ".tmp";
		Common::File::CreateDirectories(m_disk_path.parent_path());
		Common::File out(tmp);
		if (out.IsInvalid()) {
			PipelineCacheLog("Shader disk cache: cannot open {} for writing",
			                 Common::PathToString(tmp));
			return;
		}
		const auto& data   = file.Data();
		constexpr size_t kMaxChunk = 256u << 20;
		for (size_t offset = 0; offset < data.size(); offset += kMaxChunk) {
			const auto chunk = std::min(kMaxChunk, data.size() - offset);
			out.Write(data.data() + offset, static_cast<uint32_t>(chunk));
		}
		// std::filesystem::remove instead of Common::File::DeleteFile:
		// Win32 headers #define DeleteFile to DeleteFileA.
		std::error_code remove_error;
		std::filesystem::remove(m_disk_path, remove_error);
		if (!Common::File::RenameFile(tmp, m_disk_path)) {
			PipelineCacheLog("Shader disk cache: rename to {} failed",
			                 Common::PathToString(m_disk_path));
			return;
		}
		PipelineCacheLog("Shader disk cache: saved {} program(s), {} byte(s) -> {}", staged.size(),
		                 file.Size(), Common::PathToString(m_disk_path));
	}

	explicit ProgramCache(vk::Device device): device(device) {
		lookup_key.static_state.reserve(MaxStaticKeyWords);
		OpenDiskCache();
	}
	~ProgramCache() {
		for (const auto& [key, entry]: programs) {
			(void)key;
			for (const auto& permutation: entry.permutations) {
				device.destroyShaderModule(permutation.handle.module, nullptr);
			}
			// The worker threads have stopped: each job either finished or was dropped.
			for (auto& queued: entry.pending) {
				if (queued.compiled.valid() && IsReady(queued.compiled)) {
					if (auto compiled = TakeResult(queued.compiled)) {
						device.destroyShaderModule(compiled->module, nullptr);
					}
				}
			}
		}
	}

	std::unordered_map<ProgramKey, SourceEntry, ProgramKeyHash> programs;
	// New sources translating on worker threads.
	std::unordered_map<ProgramKey, std::future<ShaderRecompiler::TranslateResult>, ProgramKeyHash>
	                                                            pending_sources;
	// Whether a shader (by hash) stores data; see StoresData.
	std::unordered_map<uint64_t, bool>                          stores_data;
	// Runs background translations; null without pipeline libraries.
	PipelineLibraryCache*                                       workers = nullptr;
	// Background translations and module compiles: running or queued, and finished so far.
	struct JobCounts {
		std::atomic<uint32_t> in_flight {0};
		std::atomic<uint64_t> finished {0};
	};
	std::shared_ptr<JobCounts>                                  jobs = std::make_shared<JobCounts>();
	ProgramKey                                                  lookup_key;
	vk::Device                                                  device;
	uint64_t                                                    next_shader_id = 0;
	std::unique_ptr<ShaderDiskCache::ShaderDiskIndex>          m_disk;
	std::filesystem::path                                       m_disk_path;
	std::string                                                 m_disk_signature;
};

PipelineCache::PipelineCache(GraphicContext& graphics)
    : m_graphics(graphics), m_program_cache(std::make_unique<ProgramCache>(graphics.device)) {
	EXIT_NOT_IMPLEMENTED(!Common::Thread::IsMainThread());
	InitializeDriverCache();
	// Linking libraries without fast linking costs about as much as a full compile.
	if (m_graphics.pipeline_library_enabled && m_graphics.pipeline_library_fast_linking) {
		m_libraries = std::make_unique<PipelineLibraryCache>(m_graphics, m_driver_cache);
	}
	// Background shader translations run on the library cache's compile threads; the
	// old worker pool is subsumed by them (KYTY_NO_ASYNC_SHADERS still stops the
	// translations alone).
	if (m_libraries != nullptr && std::getenv("KYTY_NO_ASYNC_SHADERS") == nullptr) {
		m_program_cache->workers = m_libraries.get();
	} else {
		PipelineCacheLog("Background shader translation: disabled");
	}
}

PipelineCache::~PipelineCache() {
	Save();
	auto destroy = [this](const auto& pipelines) {
		for (const auto& [key, pipeline]: pipelines) {
			(void)key;
			m_graphics.device.destroyPipeline(pipeline->pipeline, nullptr);
			m_graphics.device.destroyPipelineLayout(pipeline->pipeline_layout, nullptr);
			m_graphics.device.destroyDescriptorSetLayout(pipeline->descriptor_set_layout, nullptr);
			if (pipeline->pixel_set_layout != nullptr) {
				m_graphics.device.destroyDescriptorSetLayout(pipeline->pixel_set_layout, nullptr);
			}
		}
	};
	destroy(m_graphics_pipelines);
	destroy(m_compute_pipelines);
	// The pipelines linked from the libraries go before the libraries themselves.
	m_libraries.reset();
	if (m_driver_cache != nullptr) {
		m_graphics.device.destroyPipelineCache(m_driver_cache, nullptr);
	}
}

void PipelineCache::InitializeDriverCache() {
	const auto title_id = PipelineCacheTitleId();
	if (title_id.empty()) {
		return;
	}
	if (KYTY_BUILD != KYTY_BUILD_RELEASE) {
		PipelineCacheLog("Vulkan pipeline cache: disabled (non-Release build)");
		return;
	}
	const std::string_view git_hash     = KYTY_GIT_HASH;
	const std::string_view git_revision = KYTY_GIT_REVISION;
	if (git_hash == "unknown" || git_revision == "unknown") {
		PipelineCacheLog("Vulkan pipeline cache: disabled (unknown git revision)");
		return;
	}
	if (git_hash.ends_with("-dirty")) {
		PipelineCacheLog("Vulkan pipeline cache: disabled (dirty build)");
		return;
	}

	m_driver_cache_path     = std::filesystem::path("_PipelineCache") / (title_id + ".bin");
	const auto path         = Common::PathToString(m_driver_cache_path);
	const bool cache_exists = Common::File::IsFileExisting(m_driver_cache_path);
	if (cache_exists) {
		PipelineCacheLog("Vulkan pipeline cache: loading {}", path);
	} else {
		PipelineCacheLog("Vulkan pipeline cache: initializing {}", path);
	}
	std::vector<uint8_t> initial_data;
	if (cache_exists) {
		Common::File file(m_driver_cache_path, Common::File::Mode::Read);
		const auto   file_size = file.IsInvalid() ? 0 : file.Size();
		const auto   signature = DriverCacheSignature(m_graphics.GetPhysicalDeviceProperties());
		if (file_size >= signature.size() + sizeof(uint64_t) &&
		    file_size <= std::numeric_limits<uint32_t>::max()) {
			std::string cached_signature(signature.size(), '\0');
			uint64_t    payload_hash = 0;
			initial_data.resize(file_size - signature.size() - sizeof(payload_hash));
			uint32_t signature_read = 0;
			uint32_t hash_read      = 0;
			uint32_t payload_read   = 0;
			file.Read(cached_signature.data(), static_cast<uint32_t>(cached_signature.size()),
			          &signature_read);
			file.Read(&payload_hash, sizeof(payload_hash), &hash_read);
			file.Read(initial_data.data(), static_cast<uint32_t>(initial_data.size()),
			          &payload_read);
			file.Close();
			if (signature_read != cached_signature.size() || hash_read != sizeof(payload_hash) ||
			    payload_read != initial_data.size() || cached_signature != signature ||
			    XXH3_64bits(initial_data.data(), initial_data.size()) != payload_hash) {
				initial_data.clear();
				PipelineCacheLog(
				    "Vulkan pipeline cache: invalidating {} (driver, emulator, or data mismatch)",
				    path);
			}
		} else {
			file.Close();
			PipelineCacheLog("Vulkan pipeline cache: invalidating {} (invalid file size)", path);
		}
	}

	vk::PipelineCacheCreateInfo create {};
	create.initialDataSize = initial_data.size();
	create.pInitialData    = initial_data.empty() ? nullptr : initial_data.data();
	auto result = m_graphics.device.createPipelineCache(&create, nullptr, &m_driver_cache);
	if (result != vk::Result::eSuccess && !initial_data.empty()) {
		PipelineCacheLog("Vulkan pipeline cache: driver rejected {} ({}); starting empty", path,
		                 vk::to_string(result));
		initial_data.clear();
		create.initialDataSize = 0;
		create.pInitialData    = nullptr;
		result = m_graphics.device.createPipelineCache(&create, nullptr, &m_driver_cache);
	}
	if (result != vk::Result::eSuccess) {
		PipelineCacheLog("Vulkan pipeline cache: disabled ({})", vk::to_string(result));
		m_driver_cache = nullptr;
		return;
	}
	if (!initial_data.empty()) {
		PipelineCacheLog("Vulkan pipeline cache: loaded {} bytes from {}", initial_data.size(),
		                 path);
	} else {
		PipelineCacheLog("Vulkan pipeline cache: initialized empty");
	}
}

void PipelineCache::Save() {
	if (m_libraries != nullptr) {
		// The link thread writes to the driver cache, which is saved and destroyed below.
		m_libraries->Stop();
	}
	m_program_cache->SaveToDisk();

	if (m_driver_cache == nullptr) {
		return;
	}

	size_t               size = 0;
	vk::Result           result;
	std::vector<uint8_t> payload;
	for (uint32_t attempt = 0; attempt < 3; attempt++) {
		size   = 0;
		result = m_graphics.device.getPipelineCacheData(m_driver_cache, &size, nullptr);
		if (result != vk::Result::eSuccess || size == 0 ||
		    size > std::numeric_limits<uint32_t>::max()) {
			break;
		}
		payload.resize(size);
		result = m_graphics.device.getPipelineCacheData(m_driver_cache, &size, payload.data());
		if (result != vk::Result::eIncomplete) {
			break;
		}
	}
	if (result != vk::Result::eSuccess || size == 0 ||
	    size > std::numeric_limits<uint32_t>::max()) {
		PipelineCacheLog("Vulkan pipeline cache: save failed ({}, {} bytes)",
		                 vk::to_string(result), size);
		return;
	}
	payload.resize(size);
	auto       prefix       = DriverCacheSignature(m_graphics.GetPhysicalDeviceProperties());
	const auto payload_hash = XXH3_64bits(payload.data(), payload.size());
	prefix.append(reinterpret_cast<const char*>(&payload_hash), sizeof(payload_hash));
	if (!Common::File::CreateDirectories(m_driver_cache_path.parent_path())) {
		PipelineCacheLog("Vulkan pipeline cache: failed to create cache directory");
		return;
	}
	auto temp_path = m_driver_cache_path;
	temp_path += ".tmp";
	Common::File file;
	uint32_t     prefix_written  = 0;
	uint32_t     payload_written = 0;
	if (file.Create(temp_path)) {
		file.Write(prefix.data(), static_cast<uint32_t>(prefix.size()), &prefix_written);
		file.Write(payload.data(), static_cast<uint32_t>(payload.size()), &payload_written);
	}
	const bool flushed = !file.IsInvalid() && file.Flush();
	file.Close();
	if (prefix_written != prefix.size() || payload_written != payload.size() || !flushed ||
	    !Common::File::RenameFile(temp_path, m_driver_cache_path)) {
		PipelineCacheLog("Vulkan pipeline cache: failed to write {}",
		                 Common::PathToString(m_driver_cache_path));
		return;
	}
	PipelineCacheLog("Vulkan pipeline cache: saved {} bytes to {}", payload.size(),
	                 Common::PathToString(m_driver_cache_path));
	m_graphics.device.destroyPipelineCache(m_driver_cache, nullptr);
	m_driver_cache = nullptr;
}

PipelineCache::GraphicsPrograms PipelineCache::GetGraphicsPrograms(
    const HW::VertexShaderInfo& vertex_regs, const HW::PixelShaderInfo& pixel_regs,
    const HW::ShaderRegisters& sh, const HW::Context& context, const HW::UserConfig& user_config,
    std::span<const Prospero::ColorComponentMapping, 8> target_export_mapping, bool pixel_active,
    std::array<ShaderVertexInputInfo, 3>& vertex_info, ShaderPixelInputInfo& pixel_info,
    ProgramWait wait) {
	const bool tess_active = user_config.GetPrimType() == Prospero::PrimitiveType::kPatch;
	std::array<ShaderParams, 3> vertex_params;
	if (tess_active) {
		vertex_params = PrepareTessellationPrograms(vertex_regs, context, vertex_info);
	} else {
		vertex_params[0] = PrepareProgram(vertex_regs, context, user_config, vertex_info[0]);
	}
	const bool mesh_active = vertex_info[0].logical_stage == ShaderType::Mesh;
	if (mesh_active) {
		EXIT_NOT_IMPLEMENTED(!m_graphics.mesh_shader_enabled);
		auto& mesh              = vertex_info[0].mesh;
		mesh.host_subgroup_size = m_graphics.subgroup_size;
		const auto& limits      = m_graphics.mesh_shader_properties;
		const auto  logical_threads =
		    mesh.threads_num[0] * mesh.threads_num[1] * mesh.threads_num[2];
		const auto host_threads = ((logical_threads + mesh.wave_size - 1u) / mesh.wave_size) *
		                          std::min(mesh.host_subgroup_size, mesh.wave_size);
		if (host_threads > limits.maxMeshWorkGroupInvocations ||
		    host_threads > limits.maxMeshWorkGroupSize[0] ||
		    mesh.max_vertices > limits.maxMeshOutputVertices ||
		    mesh.max_primitives > limits.maxMeshOutputPrimitives ||
		    mesh.lds_size_dwords * sizeof(uint32_t) > limits.maxMeshSharedMemorySize) {
			EXIT("mesh shader exceeds host limits: threads=%u vertices=%u primitives=%u LDS=%u\n",
			     host_threads, mesh.max_vertices, mesh.max_primitives, mesh.lds_size_dwords);
		}
	}
	ShaderParams pixel_params;
	if (pixel_active) {
		pixel_params      = PrepareProgram(pixel_regs, sh, target_export_mapping, pixel_info);
		const auto& blend = context.GetBlendControl(0);
		pixel_info.dual_source_blending =
		    blend.enable && !context.GetRenderTarget(0).info.blend_bypass &&
		    (BlendFactorIsDualSource(blend.color_srcblend) ||
		     BlendFactorIsDualSource(blend.color_destblend) ||
		     (blend.separate_alpha_blend && (BlendFactorIsDualSource(blend.alpha_srcblend) ||
		                                     BlendFactorIsDualSource(blend.alpha_destblend))));
		if (pixel_info.dual_source_blending) {
			// MRT1 supplies the second blend source for target 0.
			pixel_info.target_output_mode[1]    = pixel_info.target_output_mode[0];
			pixel_info.target_export_mapping[1] = pixel_info.target_export_mapping[0];
		} else if (blend.enable && !context.GetRenderTarget(0).info.blend_bypass &&
		           pixel_info.target_output_mode[0] != 0 && pixel_info.target_output_mode[0] != 7 &&
		           std::all_of(std::begin(pixel_info.target_output_mode) + 1,
		                       std::end(pixel_info.target_output_mode),
		                       [](uint8_t mode) { return mode == 0; }) &&
		           ClassifyBlendMapping(blend, pixel_info.target_export_mapping[0]) ==
		               BlendMappingSupport::SourceAlpha) {
			// Preserve logical alpha when the export mapping moves it.
			pixel_info.alpha_blend_source_remap = true;
			pixel_info.dual_source_blending     = true;
			pixel_info.target_output_mode[1]    = pixel_info.target_output_mode[0];
			pixel_info.target_export_mapping[1] = {};
		}
	}
	if (context.GetClipControl().clip_disable) {
		const auto& viewport = context.GetScreenViewport().viewports[0];
		const auto& limits   = m_graphics.GetPhysicalDeviceProperties().limits;
		auto&       clip     = vertex_info[tess_active ? 2u : 0u].clip_space;
		clip.scale[0]        = viewport.xscale;
		clip.scale[1]        = viewport.yscale;
		clip.offset[0]       = viewport.xoffset;
		clip.offset[1]       = viewport.yoffset;
		clip.half_extent[0] =
		    static_cast<float>(std::min(limits.maxViewportDimensions[0], 16384u)) * 0.5f;
		clip.half_extent[1] =
		    static_cast<float>(std::min(limits.maxViewportDimensions[1], 16384u)) * 0.5f;
		clip.enabled = true;
	}
	uint32_t          push_data_cursor =
	    mesh_active ? ShaderRecompiler::IR::PushData::MeshDrawDwordCount : 0;
	if (wait == ProgramWait::Defer) {
		// A draw that stores data is never skipped (see StoresData in renderDraw.cpp),
		// so its shaders compile now.
		bool stores = pixel_active && m_program_cache->StoresData(ShaderType::Pixel, pixel_params);
		for (uint32_t i = 0; i < (tess_active ? 3u : 1u) && !stores; i++) {
			stores = m_program_cache->StoresData(vertex_info[i].logical_stage, vertex_params[i]);
		}
		if (stores) {
			wait = ProgramWait::Wait;
		}
	}
	// Kyty-032: the vertex-stage translations start before the pixel stage compiles on
	// this thread; a stage a later lookup finds still translating is compiled by a worker.
	for (uint32_t i = 0; i < (tess_active ? 3u : 1u); i++) {
		m_program_cache->QueueSourceTranslation(vertex_params[i], vertex_info[i],
		                                        vertex_info[i].logical_stage);
	}
	// A pending stage stops the lookups: the push data position of the next stage depends
	// on it. A look-ahead still starts translating the later stages, which that position
	// does not affect.
	const auto translate_rest = [&](uint32_t first_vertex) {
		if (wait == ProgramWait::Prefetch) {
			for (uint32_t i = first_vertex; i < (tess_active ? 3u : 1u); i++) {
				m_program_cache->QueueSourceTranslation(vertex_params[i], vertex_info[i],
				                                    vertex_info[i].logical_stage);
			}
		}
	};
	GraphicsPrograms  result;
	if (pixel_active) {
		result.pixel = m_program_cache->Get(pixel_params, pixel_info, push_data_cursor, wait,
		                                     &result.pending);
		if (result.pending) {
			translate_rest(0);
			return result;
		}
	}
	for (uint32_t i = 0; i < (tess_active ? 3u : 1u); i++) {
		result.vertex[i] = m_program_cache->Get(vertex_params[i], vertex_info[i],
		                                        push_data_cursor, wait, &result.pending);
		if (result.pending) {
			translate_rest(i + 1);
			return result;
		}
	}
	return result;
}

ShaderProgram PipelineCache::GetComputeProgram(const HW::ComputeShaderInfo& regs,
                                               const HW::ShaderRegisters&   sh,
                                               ShaderComputeInputInfo&      input_info,
                                               ProgramWait wait, bool* pending) {
	input_info.host_subgroup_size = m_graphics.SupportsComputeWave64() ? 64u : 32u;
	const auto        params      = PrepareProgram(regs, sh, input_info);
	uint32_t          push_data_cursor = 0;
	return m_program_cache->Get(params, input_info, push_data_cursor, wait, pending);
}

bool PipelineStaticParameters::operator==(const PipelineStaticParameters& other) const noexcept {
	return std::memcmp(this, &other, sizeof(*this)) == 0;
}

PipelineCache::Pipeline* PipelineCache::GetGraphicsPipeline(
    std::span<const RenderColorInfo> colors, const RenderDepthInfo& depth,
    std::span<const ShaderVertexInputInfo> vertex_info, CommandBuffer& command,
    const ShaderPixelInputInfo* ps_input_info, vk::PrimitiveTopology topology,
    bool primitive_restart_enable, const GraphicsPrograms& programs, bool may_defer) {
	const auto& vs_input_info  = vertex_info.front();
	const auto& vertex_program = programs.vertex[0];
	const auto& pixel_program  = programs.pixel;
	KYTY_PROFILER_BLOCK("PipelineCache::CreatePipeline(Gfx)", profiler::colors::DeepOrangeA200);

	EXIT_IF(colors.size() > RENDER_COLOR_ATTACHMENTS_MAX);
	EXIT_IF(!vertex_program);
	const bool ps_active = ps_input_info != nullptr;
	EXIT_IF(ps_active && !pixel_program);
	const auto color_count = static_cast<uint32_t>(colors.size());

	auto&             ctx = command.GetRegisters();

	const HW::ModeControl& mc = ctx.GetModeControl();

	const auto vs_id = vertex_program.id;
	const auto ps_id = ps_active ? pixel_program.id : 0;

	GraphicsPipelineKey key {};
	for (uint32_t i = 0; i < programs.vertex.size(); i++) {
		key.vertex_shader_ids[i] = programs.vertex[i].id;
	}
	key.ps_shader_id            = ps_id;
	auto& static_params         = key.static_params;
	auto& rendering             = key.rendering;
	rendering.color_count       = 0;
	uint32_t attachment_samples = 0;
	for (uint32_t i = 0; i < color_count; i++) {
		const auto slot = colors[i].target_slot;
		EXIT_IF(slot >= RENDER_COLOR_ATTACHMENTS_MAX);
		rendering.color_count = std::max(rendering.color_count, slot + 1);
		EXIT_IF(!colors[i].image_id || colors[i].desc.view_info.format == vk::Format::eUndefined);
		static_params.color_mask[slot] = colors[i].export_mapping.ApplyMask(
		    render_target_mask_slot(ctx.GetRenderTargetMask(), colors[i].target_slot));
		rendering.color_formats[slot] = colors[i].desc.view_info.format;
		if (attachment_samples == 0) {
			attachment_samples = colors[i].desc.info.samples;
		} else if (attachment_samples != colors[i].desc.info.samples) {
			EXIT("mixed color attachment sample counts are unsupported: %u and %u\n",
			     attachment_samples, colors[i].desc.info.samples);
		}
		const auto& rt                        = ctx.GetRenderTarget(colors[i].target_slot);
		const auto& bc                        = ctx.GetBlendControl(colors[i].target_slot);
		static_params.color_srcblend[slot]       = bc.color_srcblend;
		static_params.color_comb_fcn[slot]       = bc.color_comb_fcn;
		static_params.color_destblend[slot]      = bc.color_destblend;
		static_params.alpha_srcblend[slot]       = bc.alpha_srcblend;
		static_params.alpha_comb_fcn[slot]       = bc.alpha_comb_fcn;
		static_params.alpha_destblend[slot]      = bc.alpha_destblend;
		static_params.separate_alpha_blend[slot] = bc.separate_alpha_blend;
		const bool alpha_remap =
		    slot == 0 && ps_input_info != nullptr && ps_input_info->alpha_blend_source_remap;
		static_params.blend_enable[slot] = bc.enable && !rt.info.blend_bypass;
		if (static_params.blend_enable[slot] && !alpha_remap &&
		    ClassifyBlendMapping(bc, colors[i].export_mapping) != BlendMappingSupport::Direct) {
			static_params.blend_enable[slot] = false;
			static std::atomic_bool warned = false;
			if (!warned.exchange(true, std::memory_order_relaxed)) {
				Log::WriteToConsoleAndLog(fmt::format(
				    "Warning: blending disabled for unsupported color mapping "
				    "(slot={} mapping=0x{:02x} color={}/{} alpha={}/{} separate={}).\n",
				    slot, colors[i].export_mapping.packed, bc.color_srcblend, bc.color_destblend,
				    bc.alpha_srcblend, bc.alpha_destblend, bc.separate_alpha_blend ? 1 : 0));
			}
		}
		if (alpha_remap) {
			static_params.blend_alpha_source_remap = true;
		}
	}
	const bool with_depth =
	    depth.desc.view_info.format != vk::Format::eUndefined && static_cast<bool>(depth.image_id);
	if (with_depth) {
		const auto aspects       = ImageViewOps::DepthAspectMask(depth.desc.view_info.format);
		rendering.depth_format   = aspects & vk::ImageAspectFlagBits::eDepth
		                               ? depth.desc.view_info.format
		                               : vk::Format::eUndefined;
		rendering.stencil_format = aspects & vk::ImageAspectFlagBits::eStencil
		                               ? depth.desc.view_info.format
		                               : vk::Format::eUndefined;
		if (attachment_samples == 0) {
			attachment_samples = depth.desc.info.samples;
		} else if (attachment_samples != depth.desc.info.samples) {
			EXIT("mixed color/depth sample counts are unsupported: %u and %u\n", attachment_samples,
			     depth.desc.info.samples);
		}
	}
	if (color_count == 0 && !with_depth) {
		attachment_samples = render_sample_count(ctx.GetAaConfig().msaa_num_samples);
		EXIT_IF(!static_cast<bool>(
		    m_graphics.GetPhysicalDeviceProperties().limits.framebufferNoAttachmentsSampleCounts &
		    vulkan_sample_count(attachment_samples)));
	}
	EXIT_IF(attachment_samples == 0 ||
	        vulkan_sample_count(attachment_samples) == vk::SampleCountFlagBits {});

	if (ps_active && depth.depth_test_enable && ps_input_info->ps_execute_on_noop) {
		static std::atomic<uint32_t> log_count {0};
		if (log_count.fetch_add(1, std::memory_order_relaxed) < 16) {
			LOGF("Pipeline: temporary: accepting EXEC_ON_NOOP with depth test enabled\n");
		}
	}

	const auto& clip_control               = ctx.GetClipControl();
	static_params.negative_one_to_one      = !clip_control.dx_clip_space;
	static_params.depth_clip_enable        = clip_control.IsZClipEnabled();
	static_params.topology                 = topology;
	static_params.primitive_restart_enable = primitive_restart_enable;
	static_params.samples                  = attachment_samples;
	static_params.sample_shading_enable =
	    ps_active && attachment_samples > 1 && ps_input_info->ps_sample_shading;
	if (static_params.sample_shading_enable && !m_graphics.sample_rate_shading_enabled) {
		EXIT("Pipeline: sample-rate shading is required but unsupported by the host\n");
	}
	static_params.depth_bounds_test_enable = depth.depth_bounds_test_enable;
	static_params.depth_min_bounds         = depth.depth_min_bounds;
	static_params.depth_max_bounds         = depth.depth_max_bounds;
	const bool rect_list = Prospero::IsRectList(command.GetUserConfig().GetPrimType());
	static_params.cull_back  = !rect_list && mc.cull_back;
	static_params.cull_front = !rect_list && mc.cull_front;
	static_params.face       = mc.face;
	static_params.provoking_vtx_last = mc.provoking_vtx_last;
	static_params.polygon_mode =
	    ResolvePolygonMode(mc, static_params.cull_front, static_params.cull_back);

	if (vs_input_info.stage.program->stage != ShaderType::Mesh) {
		EXIT_IF(vs_input_info.buffers_num < 0 ||
		        vs_input_info.buffers_num > ShaderVertexInputInfo::RES_MAX ||
		        vs_input_info.resources_num < 0 ||
		        vs_input_info.resources_num > ShaderVertexInputInfo::RES_MAX);
		key.vertex_input.binding_count   = static_cast<uint8_t>(vs_input_info.buffers_num);
		key.vertex_input.attribute_count = static_cast<uint8_t>(vs_input_info.resources_num);
		for (int binding = 0; binding < vs_input_info.buffers_num; binding++) {
			const auto& buffer = vs_input_info.buffers[binding];
			key.vertex_input.bindings[binding] = {.stride   = buffer.stride,
			                                      .instance = buffer.fetch_index != 0};
		}
		for (int attribute = 0; attribute < vs_input_info.resources_num; attribute++) {
			const auto binding = vs_input_info.resources_dst[attribute].buffer_index;
			EXIT_IF(binding < 0 || binding >= vs_input_info.buffers_num);
			key.vertex_input.attributes[attribute] = {
			    .offset = static_cast<uint32_t>(vs_input_info.resources[attribute].Base48() -
			                                    vs_input_info.buffers[binding].addr),
			    .binding = static_cast<uint8_t>(binding),
			};
		}
	}

	if (auto iter = m_graphics_pipelines.find(key); iter != m_graphics_pipelines.end()) {
		auto& found = *iter->second;
		if (found.optimize_pending) [[unlikely]] {
			InstallOptimizedPipeline(found, command);
		}
		return &found;
	}

	if (graphics_debug_dump_enabled()) {
		ShaderDbgDumpInputInfo(vs_input_info);
		if (ps_active) {
			ShaderDbgDumpInputInfo(*ps_input_info);
		}
		LOGF("PipelineTrace: shader modules VS=%" PRIu64 " module=%p PS=%" PRIu64 " module=%p\n",
		     vs_id, static_cast<void*>(vertex_program.module), ps_id,
		     static_cast<void*>(pixel_program.module));
	}

	if (may_defer && m_libraries != nullptr && Config::PipelineLibrariesEnabled()) {
		// Queue the shader parts no earlier pipeline built, and skip the draw until they
		// are compiled; the other two parts and the link take about a millisecond.
		bool ready = false;
		PrefetchLibraryParts(m_graphics, rendering, key.vertex_input, vertex_info,
		                     ps_input_info, programs, static_params, *m_libraries,
		                     m_driver_cache, &ready);
		if (!ready) {
			m_deferred_draws[key]++;
			return nullptr;
		}
	}
	// The draws this pipeline skipped while its parts were compiling are done waiting.
	if (auto deferred = m_deferred_draws.find(key); deferred != m_deferred_draws.end()) {
		m_deferred_draws.erase(deferred);
	}

	auto cached = std::make_unique<Pipeline>();
	LogPipelineTrace("CreatePipelineInternal begin", vs_id, ps_id);
	CreatePipelineInternal(m_graphics, *cached, rendering, key.vertex_input, vertex_info,
	                       ps_input_info, programs, static_params, m_libraries.get(),
	                       m_driver_cache);
	m_graphics_pipelines_created++;
	LogPipelineTrace("CreatePipelineInternal done", vs_id, ps_id);

	EXIT_NOT_IMPLEMENTED(cached->pipeline == nullptr);
	EXIT_NOT_IMPLEMENTED(cached->pipeline_layout == nullptr);

	auto [iter, inserted] = m_graphics_pipelines.emplace(std::move(key), std::move(cached));
	EXIT_IF(!inserted);

	return iter->second.get();
}

void PipelineCache::InstallOptimizedPipeline(Pipeline& pipeline, CommandBuffer& command) {
	const auto optimized = m_libraries->TakeOptimized(&pipeline);
	if (!optimized) {
		return;
	}
	pipeline.optimize_pending = false;
	if (*optimized == nullptr) {
		return;
	}
	// Commands recorded before this draw may still use the fast-linked pipeline.
	const auto fast_linked = pipeline.pipeline;
	pipeline.pipeline      = *optimized;
	command.GetContext().GetCommandScheduler().DeferOperation(
	    [device = m_graphics.device, fast_linked] { device.destroyPipeline(fast_linked, nullptr); });
}

PipelineCache::Pipeline&
PipelineCache::GetComputePipeline(const ShaderComputeInputInfo& input_info,
                                  const ShaderProgram&          compute_program) {
	KYTY_PROFILER_BLOCK("PipelineCache::CreatePipeline(Compute)", profiler::colors::RedA100);

	EXIT_IF(!compute_program);

	if (auto iter = m_compute_pipelines.find(compute_program.id);
	    iter != m_compute_pipelines.end()) {
		return *iter->second;
	}

	if (graphics_debug_dump_enabled()) {
		ShaderDbgDumpInputInfo(input_info);
	}

	auto cached = std::make_unique<Pipeline>();
	CreatePipelineInternal(m_graphics, *cached, input_info, compute_program.module, m_driver_cache);
	m_compute_pipelines_created++;

	EXIT_NOT_IMPLEMENTED(cached->pipeline == nullptr);
	EXIT_NOT_IMPLEMENTED(cached->pipeline_layout == nullptr);

	auto [iter, inserted] = m_compute_pipelines.emplace(compute_program.id, std::move(cached));
	EXIT_IF(!inserted);

	return *iter->second;
}
} // namespace Libs::Graphics
