#include "graphics/host_gpu/renderer/pipeline/shaderDiskCache.h"

#include "common/file.h"
#include "common/logging/log.h"

#include <bit>
#include <limits>

namespace Libs::Graphics::ShaderDiskCache {

namespace {

using namespace Libs::Graphics::ShaderRecompiler;

using InstIndex = std::unordered_map<const IR::Inst*, uint32_t>;

} // namespace

// ---------------------------------------------------------------- Reader ---

bool Reader::Read(void* dst, size_t size) {
		if (!m_ok || size > m_size - m_pos) {
				m_ok = false;
				return false;
		}
		std::memcpy(dst, m_data + m_pos, size);
		m_pos += size;
		return true;
}

uint8_t Reader::ReadU8() {
		uint8_t value = 0;
		if (!Read(&value, sizeof(value))) {
				return 0;
		}
		return value;
}

uint32_t Reader::ReadU32() {
		uint32_t value = 0;
		if (!Read(&value, sizeof(value))) {
				return 0;
		}
		return value;
}

uint64_t Reader::ReadU64() {
		uint64_t value = 0;
		if (!Read(&value, sizeof(value))) {
				return 0;
		}
		return value;
}

bool Reader::ReadBool() {
		return ReadU8() != 0;
}

std::string Reader::ReadString() {
		const auto size = ReadU32();
		if (!m_ok || size > MaxStringLength) {
				m_ok = false;
				return {};
		}
		std::string value(size, '\0');
		if (size != 0 && !Read(value.data(), size)) {
				return {};
		}
		return value;
}

std::vector<uint32_t> Reader::ReadWords() {
		const auto count = ReadU32();
		if (!m_ok || count > MaxVectorElements) {
				m_ok = false;
				return {};
		}
		std::vector<uint32_t> value(count);
		if (count != 0 && !Read(value.data(), static_cast<size_t>(count) * sizeof(uint32_t))) {
				return {};
		}
		return value;
}

std::vector<uint8_t> Reader::ReadBytes() {
		const auto count = ReadU32();
		if (!m_ok || count > MaxVectorElements) {
				m_ok = false;
				return {};
		}
		std::vector<uint8_t> value(count);
		if (count != 0 && !Read(value.data(), count)) {
				return {};
		}
		return value;
}

// ---------------------------------------------------------------- Writer ---

void Writer::Write(const void* data, size_t size) {
		const auto* bytes = static_cast<const uint8_t*>(data);
		m_data.insert(m_data.end(), bytes, bytes + size);
}

void Writer::WriteU8(uint8_t value) {
		Write(&value, sizeof(value));
}

void Writer::WriteU32(uint32_t value) {
		Write(&value, sizeof(value));
}

void Writer::WriteU64(uint64_t value) {
		Write(&value, sizeof(value));
}

void Writer::WriteBool(bool value) {
		WriteU8(value ? 1 : 0);
}

void Writer::WriteString(std::string_view value) {
		WriteU32(static_cast<uint32_t>(value.size()));
		Write(value.data(), value.size());
}

void Writer::WriteWords(std::span<const uint32_t> value) {
		WriteU32(static_cast<uint32_t>(value.size()));
		Write(value.data(), value.size_bytes());
}

void Writer::WriteBytes(std::span<const uint8_t> value) {
		WriteU32(static_cast<uint32_t>(value.size()));
		Write(value.data(), value.size());
}

// ------------------------------------------------------------------- Key ---

std::size_t KeyHash::operator()(const Key& key) const {
		// Same mixing as the in-memory program cache hash.
		std::size_t hash = static_cast<std::size_t>(key.stage);
		hash ^= static_cast<std::size_t>(key.hash) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
		hash ^= key.user_data_count + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
		hash ^= key.code_size + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
		hash ^= key.static_state.size() + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
		return hash;
}

// ----------------------------------------------------------------- Value ---

bool WriteValue(Writer& w, const IR::Value& value, const InstIndex& index) {
		const auto type_bits = value.RawTypeBits();
		w.WriteU32(type_bits);
		if (type_bits == static_cast<uint32_t>(IR::Type::Opaque)) {
				const auto* inst = value.TryInstruction();
				const auto  it   = inst != nullptr ? index.find(inst) : index.end();
				if (inst == nullptr || it == index.end()) {
						return false;
				}
				w.WriteU64(it->second);
				return true;
		}
		w.WriteU64(value.RawPayloadBits());
		return true;
}

std::optional<IR::Value> ReadValue(Reader& r, const std::vector<IR::Inst*>& insts) {
		const auto type_bits = r.ReadU32();
		if (!r.Ok()) {
				return std::nullopt;
		}
		if (type_bits == static_cast<uint32_t>(IR::Type::Opaque)) {
				const auto index = r.ReadU64();
				if (!r.Ok() || index >= insts.size()) {
						return std::nullopt;
				}
				return IR::Value(insts[static_cast<size_t>(index)]);
		}
		const auto bits = r.ReadU64();
		if (!r.Ok()) {
				return std::nullopt;
		}
		switch (static_cast<IR::Type>(type_bits)) {
				case IR::Type::Void:      return IR::Value();
				case IR::Type::ScalarReg: return IR::Value(static_cast<IR::ScalarReg>(static_cast<uint16_t>(bits)));
				case IR::Type::VectorReg: return IR::Value(static_cast<IR::VectorReg>(static_cast<uint16_t>(bits)));
				case IR::Type::U1:        return IR::Value(bits != 0);
				case IR::Type::U8:        return IR::Value(static_cast<uint8_t>(bits));
				case IR::Type::U16:       return IR::Value(static_cast<uint16_t>(bits));
				case IR::Type::U32:       return IR::Value(static_cast<uint32_t>(bits));
				case IR::Type::U64:       return IR::Value(bits);
				case IR::Type::F16:       return IR::Value::F16(static_cast<uint16_t>(bits));
				case IR::Type::F32:       return IR::Value::F32(std::bit_cast<float>(static_cast<uint32_t>(bits)));
				default:                  return std::nullopt; // not an immediate-carrying type
		}
}

// ------------------------------------------------------- ShaderInfo etc. ---

bool WriteShaderInfo(Writer& w, const IR::ShaderInfo& info) {
		w.WriteU32(static_cast<uint32_t>(info.buffers.size()));
		for (const auto& b: info.buffers) {
				w.WriteU32(b.source);
				w.WriteU32(b.first_use_pc);
				w.WriteU32(b.max_byte_extent);
				w.WriteU32(b.packed_stride);
				w.WriteEnum(b.descriptor_format);
				w.WriteU32(b.descriptor_swizzle);
				w.WriteU32(b.image_alias);
				w.WriteBool(b.read);
				w.WriteBool(b.written);
				w.WriteBool(b.atomic);
				w.WriteBool(b.formatted);
				w.WriteBool(b.scalar);
		}

		w.WriteU32(static_cast<uint32_t>(info.images.size()));
		for (const auto& i: info.images) {
				w.WriteU32(i.source);
				w.WriteU32(i.first_use_pc);
				w.WriteEnum(i.resource_class);
				w.WriteEnum(i.numeric_class);
				w.WriteEnum(i.dimension);
				w.WriteEnum(i.mip_mode);
				w.WriteU32(i.mip_count);
				w.WriteEnum(i.conversion_format);
				w.WriteU32(i.shader_swizzle);
				w.WriteBool(i.read);
				w.WriteBool(i.written);
				w.WriteBool(i.atomic);
				w.WriteBool(i.depth_compare);
				w.WriteBool(i.cube);
				w.WriteBool(i.r128);
				w.WriteU32(i.indirect_root);
				w.WriteU32(i.indirect_mapping_offset);
				w.WriteU32(i.indirect_search_iterations);
				w.WriteWords(i.indirect_resources);
		}

		w.WriteU32(static_cast<uint32_t>(info.samplers.size()));
		for (const auto& s: info.samplers) {
				w.WriteU32(s.source);
				w.WriteU32(s.first_use_pc);
				w.WriteBool(s.force_point_filtering);
				w.WriteBool(s.depth_compare);
				w.WriteBool(s.integer_border);
		}

		w.WriteU32(static_cast<uint32_t>(info.sampled_pairs.size()));
		for (const auto& p: info.sampled_pairs) {
				w.WriteU32(p.image);
				w.WriteU32(p.sampler);
				w.WriteU32(p.first_use_pc);
		}

		w.WriteU32(static_cast<uint32_t>(info.inputs.size()));
		for (const auto& in: info.inputs) {
				w.WriteEnum(in.kind);
				w.WriteU32(in.location);
				w.WriteU32(in.component_count);
				w.WriteString(in.debug_name);
				w.WriteBool(in.per_vertex);
		}

		w.WriteU32(static_cast<uint32_t>(info.outputs.size()));
		for (const auto& out: info.outputs) {
				w.WriteEnum(out.kind);
				w.WriteU32(out.index);
				w.WriteU32(out.location);
				w.WriteString(out.debug_name);
		}

		w.Write(static_cast<const void*>(info.vertex_fetch_components.data()),
				info.vertex_fetch_components.size());
		w.WriteU32(static_cast<uint32_t>(info.vertex_offset_sgpr));
		w.WriteU32(static_cast<uint32_t>(info.instance_offset_sgpr));
		w.WriteBool(info.has_bitwise_xor);
		w.WriteBool(info.uses_dma);
		return true;
}

std::optional<IR::ShaderInfo> ReadShaderInfo(Reader& r) {
		IR::ShaderInfo info;

		const auto buffer_count = r.ReadU32();
		if (!r.Ok() || buffer_count > MaxVectorElements) {
				return std::nullopt;
		}
		info.buffers.resize(buffer_count);
		for (auto& b: info.buffers) {
				b.source             = r.ReadU32();
				b.first_use_pc       = r.ReadU32();
				b.max_byte_extent    = r.ReadU32();
				b.packed_stride      = r.ReadU32();
				b.descriptor_format  = r.ReadEnum<Prospero::BufferFormat>();
				b.descriptor_swizzle = r.ReadU32();
				b.image_alias        = r.ReadU32();
				b.read               = r.ReadBool();
				b.written            = r.ReadBool();
				b.atomic             = r.ReadBool();
				b.formatted          = r.ReadBool();
				b.scalar             = r.ReadBool();
		}

		const auto image_count = r.ReadU32();
		if (!r.Ok() || image_count > MaxVectorElements) {
				return std::nullopt;
		}
		info.images.resize(image_count);
		for (auto& i: info.images) {
				i.source                    = r.ReadU32();
				i.first_use_pc              = r.ReadU32();
				i.resource_class            = r.ReadEnum<IR::ImageResourceClass>();
				i.numeric_class             = r.ReadEnum<Prospero::TextureNumericClass>();
				i.dimension                 = r.ReadEnum<Decoder::ImageDimension>();
				i.mip_mode                  = r.ReadEnum<IR::ImageMipMode>();
				i.mip_count                 = r.ReadU32();
				i.conversion_format         = r.ReadEnum<Prospero::BufferFormat>();
				i.shader_swizzle            = r.ReadU32();
				i.read                      = r.ReadBool();
				i.written                   = r.ReadBool();
				i.atomic                    = r.ReadBool();
				i.depth_compare             = r.ReadBool();
				i.cube                      = r.ReadBool();
				i.r128                      = r.ReadBool();
				i.indirect_root             = r.ReadU32();
				i.indirect_mapping_offset   = r.ReadU32();
				i.indirect_search_iterations = r.ReadU32();
				i.indirect_resources        = r.ReadWords();
		}

		const auto sampler_count = r.ReadU32();
		if (!r.Ok() || sampler_count > MaxVectorElements) {
				return std::nullopt;
		}
		info.samplers.resize(sampler_count);
		for (auto& s: info.samplers) {
				s.source                = r.ReadU32();
				s.first_use_pc          = r.ReadU32();
				s.force_point_filtering = r.ReadBool();
				s.depth_compare         = r.ReadBool();
				s.integer_border        = r.ReadBool();
		}

		const auto pair_count = r.ReadU32();
		if (!r.Ok() || pair_count > MaxVectorElements) {
				return std::nullopt;
		}
		info.sampled_pairs.resize(pair_count);
		for (auto& p: info.sampled_pairs) {
				p.image        = r.ReadU32();
				p.sampler      = r.ReadU32();
				p.first_use_pc = r.ReadU32();
		}

		const auto input_count = r.ReadU32();
		if (!r.Ok() || input_count > MaxVectorElements) {
				return std::nullopt;
		}
		info.inputs.resize(input_count);
		for (auto& in: info.inputs) {
				in.kind            = r.ReadEnum<IR::StageInputKind>();
				in.location        = r.ReadU32();
				in.component_count = r.ReadU32();
				in.debug_name      = r.ReadString();
				in.per_vertex      = r.ReadBool();
		}

		const auto output_count = r.ReadU32();
		if (!r.Ok() || output_count > MaxVectorElements) {
				return std::nullopt;
		}
		info.outputs.resize(output_count);
		for (auto& out: info.outputs) {
				out.kind       = r.ReadEnum<IR::StageOutputKind>();
				out.index      = r.ReadU32();
				out.location   = r.ReadU32();
				out.debug_name = r.ReadString();
		}

		if (!r.Read(info.vertex_fetch_components.data(), info.vertex_fetch_components.size())) {
				return std::nullopt;
		}
		info.vertex_offset_sgpr   = static_cast<int32_t>(r.ReadU32());
		info.instance_offset_sgpr = static_cast<int32_t>(r.ReadU32());
		info.has_bitwise_xor      = r.ReadBool();
		info.uses_dma             = r.ReadBool();

		if (!r.Ok()) {
				return std::nullopt;
		}
		return info;
}

bool WriteCompiledShaderInfo(Writer& w, const IR::CompiledShaderInfo& program) {
		w.WriteEnum(program.stage);
		w.WriteU64(program.shader_hash);
		w.WriteU32(program.wave_size);
		w.WriteU32(program.user_data_base);
		w.WriteU32(program.user_data_count);
		w.WriteU32(program.scratch_dwords);
		w.WriteU32(program.param_export_mask);
		WriteShaderInfo(w, program.info);

		const auto& layout = program.bindings;
		w.WriteU32(layout.push_data_start_dword);
		w.WriteU32(layout.memory_offset_dword);
		w.WriteU32(layout.memory_offset_count);
		w.WriteWords(layout.user_data_registers);
		w.WriteU32(static_cast<uint32_t>(layout.descriptors.size()));
		for (const auto& d: layout.descriptors) {
				w.WriteEnum(d.kind);
				w.WriteWords(d.resources);
		}
		return true;
}

std::optional<IR::CompiledShaderInfo> ReadCompiledShaderInfo(Reader& r) {
		IR::CompiledShaderInfo program;
		program.stage             = r.ReadEnum<ShaderType>();
		program.shader_hash       = r.ReadU64();
		program.wave_size         = r.ReadU32();
		program.user_data_base    = r.ReadU32();
		program.user_data_count   = r.ReadU32();
		program.scratch_dwords    = r.ReadU32();
		program.param_export_mask = r.ReadU32();

		auto info = ReadShaderInfo(r);
		if (!info) {
				return std::nullopt;
		}
		program.info = std::move(*info);

		auto& layout               = program.bindings;
		layout.push_data_start_dword = r.ReadU32();
		layout.memory_offset_dword   = r.ReadU32();
		layout.memory_offset_count   = r.ReadU32();
		layout.user_data_registers   = r.ReadWords();

		const auto descriptor_count = r.ReadU32();
		if (!r.Ok() || descriptor_count > MaxVectorElements) {
				return std::nullopt;
		}
		layout.descriptors.resize(descriptor_count);
		for (auto& d: layout.descriptors) {
				d.kind      = r.ReadEnum<IR::DescriptorBindingKind>();
				d.resources = r.ReadWords();
		}

		if (!r.Ok()) {
				return std::nullopt;
		}
		return program;
}

bool WriteResourceSpecialization(Writer& w, const IR::ResourceSpecialization& spec) {
		w.WriteU32(static_cast<uint32_t>(spec.buffers.size()));
		for (const auto& b: spec.buffers) {
				w.WriteU32(b.packed_stride);
				w.WriteEnum(b.descriptor_format);
				w.WriteU32(b.descriptor_swizzle);
				w.WriteBool(b.zero_stride_oob);
		}
		w.WriteU32(static_cast<uint32_t>(spec.images.size()));
		for (const auto& i: spec.images) {
				w.WriteEnum(i.numeric_class);
				w.WriteEnum(i.dimension);
				w.WriteU32(i.mip_count);
				w.WriteEnum(i.conversion_format);
				w.WriteU32(i.shader_swizzle);
				w.WriteU32(i.indirect_root);
				w.WriteU32(i.indirect_mapping_offset);
				w.WriteU32(i.indirect_search_iterations);
				w.WriteBool(i.cube);
				w.WriteBool(i.fmask);
		}
		return true;
}

std::optional<IR::ResourceSpecialization> ReadResourceSpecialization(Reader& r) {
		IR::ResourceSpecialization spec;

		const auto buffer_count = r.ReadU32();
		if (!r.Ok() || buffer_count > MaxVectorElements) {
				return std::nullopt;
		}
		spec.buffers.resize(buffer_count);
		for (auto& b: spec.buffers) {
				b.packed_stride      = r.ReadU32();
				b.descriptor_format  = r.ReadEnum<Prospero::BufferFormat>();
				b.descriptor_swizzle = r.ReadU32();
				b.zero_stride_oob    = r.ReadBool();
		}

		const auto image_count = r.ReadU32();
		if (!r.Ok() || image_count > MaxVectorElements) {
				return std::nullopt;
		}
		spec.images.resize(image_count);
		for (auto& i: spec.images) {
				i.numeric_class              = r.ReadEnum<Prospero::TextureNumericClass>();
				i.dimension                  = r.ReadEnum<Decoder::ImageDimension>();
				i.mip_count                  = r.ReadU32();
				i.conversion_format          = r.ReadEnum<Prospero::BufferFormat>();
				i.shader_swizzle             = r.ReadU32();
				i.indirect_root              = r.ReadU32();
				i.indirect_mapping_offset    = r.ReadU32();
				i.indirect_search_iterations = r.ReadU32();
				i.cube                       = r.ReadBool();
				i.fmask                      = r.ReadBool();
		}

		if (!r.Ok()) {
				return std::nullopt;
		}
		return spec;
}

// ----------------------------------------------------------- ResourcePlan ---
// Wire order note: the instruction storage is written BEFORE the plan fields
// whose Values reference it, so a reader can rebuild every node first and then
// decode the referencing fields in a single forward pass.

bool WriteResourcePlan(Writer& w, const IR::ResourcePlan& plan) {
		InstIndex index;
		uint32_t  next = 0;
		for (const auto& inst: plan.value_storage) {
				index.emplace(&inst, next++);
		}

		w.WriteEnum(plan.stage);
		w.WriteU64(plan.shader_hash);
		w.WriteU32(plan.user_data_base);
		w.WriteU32(plan.user_data_count);
		w.WriteBool(plan.requires_specialization_memory);
		w.WriteBool(plan.capture_specialization_reads);
		w.WriteBool(plan.srt_plan_complete);
		w.WriteBool(plan.resource_tracking_complete);
		if (!WriteShaderInfo(w, plan.info)) {
				return false;
		}

		w.WriteU32(static_cast<uint32_t>(plan.memory_info.size()));
		for (const auto& m: plan.memory_info) {
				w.WriteEnum(m.kind);
				w.WriteU32(m.resource);
				w.WriteU32(m.sampler);
				w.WriteU32(m.offset);
				w.WriteU32(m.secondary_offset);
				w.WriteU32(m.dmask);
				w.WriteU32(m.data_dwords);
				w.WriteU32(m.data_bits);
				w.WriteU32(m.component_index);
				w.WriteU32(m.component_count);
				w.WriteU32(m.data_format);
				w.WriteU32(m.number_format);
				w.WriteU32(m.image_sample_flags);
				w.WriteEnum(m.image_dimension);
				w.WriteU32(m.image_address_components);
				w.WriteBool(m.address_is_full);
				w.WriteBool(m.data_signed);
				w.WriteBool(m.typed);
				w.WriteBool(m.formatted);
				w.WriteBool(m.image_has_mip);
				w.WriteBool(m.image_r128);
				w.WriteBool(m.idxen);
				w.WriteBool(m.offen);
				w.WriteBool(m.coherent);
				w.WriteBool(m.planning_only);
		}

		w.WriteU32(static_cast<uint32_t>(plan.value_storage.size()));
		for (const auto& inst: plan.value_storage) {
				w.WriteU32(static_cast<uint32_t>(inst.GetOpcode()));
				w.WriteU64(inst.Flags<uint64_t>());
				w.WriteU32(static_cast<uint32_t>(inst.NumArgs()));
		}
		for (const auto& inst: plan.value_storage) {
				for (size_t a = 0; a < inst.NumArgs(); a++) {
						if (!WriteValue(w, inst.Arg(a), index)) {
								return false;
						}
				}
		}

		w.WriteU32(static_cast<uint32_t>(plan.descriptor_sources.size()));
		for (const auto& s: plan.descriptor_sources) {
				w.WriteU32(s.dword_count);
				for (uint32_t d = 0; d < s.dword_count; d++) {
						if (!WriteValue(w, s.dwords[d], index)) {
								return false;
						}
				}
				if (s.indirect_descriptor.has_value()) {
						w.WriteBool(true);
						const auto& indirect = *s.indirect_descriptor;
						const bool has_selector = indirect.selector.has_value();
						w.WriteBool(has_selector);
						if (has_selector) {
							w.WriteU32(indirect.selector->source);
							w.WriteU32(indirect.selector->stride);
							w.WriteU32(indirect.selector->offset);
						}
						w.WriteU32(indirect.table_source);
						w.WriteU32(indirect.table_offset);
						w.WriteU32(indirect.table_immediate);
						w.WriteU32(indirect.table_stride);
						w.WriteU32(indirect.table_record_bytes);
						w.WriteBool(indirect.table_scalar);
						w.WriteU32(indirect.workgroup_axis);
						if (!WriteValue(w, indirect.key_count, index)) {
								return false;
						}
						if (!WriteValue(w, indirect.selector_first, index)) {
								return false;
						}
						if (!WriteValue(w, indirect.selector_mask, index)) {
								return false;
						}
						w.WriteWords(indirect.sources);
				} else {
						w.WriteBool(false);
				}
		}

		w.WriteU32(static_cast<uint32_t>(plan.control_flow.size()));
		for (const auto& block: plan.control_flow) {
				if (!WriteValue(w, block.condition, index)) {
						return false;
				}
				w.WriteWords(block.successors);
				w.WriteWords(block.sources);
		}

		w.WriteU32(static_cast<uint32_t>(plan.srt_reads.size()));
		for (const auto& read: plan.srt_reads) {
				if (!WriteValue(w, read.value, index)) {
						return false;
				}
				w.WriteU32(read.flat_offset);
		}

		w.WriteBytes(plan.clean_flat_slots);

		const auto& fill = plan.uniform_fill.fill;
		w.WriteEnum(fill.kind);
		w.WriteU32(fill.resource);
		for (const auto stride: fill.group_stride) {
				w.WriteU32(stride);
		}
		w.WriteU32(fill.words);
		w.WriteU32(fill.value);
		for (uint32_t i = 0; i < fill.words; i++) {
				if (!WriteValue(w, plan.uniform_fill.values[i], index)) {
						return false;
				}
		}
		return true;
}

std::optional<IR::ResourcePlan> ReadResourcePlan(Reader& r) {
		IR::ResourcePlan plan;

		plan.stage           = r.ReadEnum<ShaderType>();
		plan.shader_hash     = r.ReadU64();
		plan.user_data_base  = r.ReadU32();
		plan.user_data_count = r.ReadU32();
		plan.requires_specialization_memory = r.ReadBool();
		plan.capture_specialization_reads   = r.ReadBool();
		plan.srt_plan_complete              = r.ReadBool();
		plan.resource_tracking_complete     = r.ReadBool();

		auto info = ReadShaderInfo(r);
		if (!info) {
				return std::nullopt;
		}
		plan.info = std::move(*info);

		const auto memory_count = r.ReadU32();
		if (!r.Ok() || memory_count > MaxVectorElements) {
				return std::nullopt;
		}
		plan.memory_info.resize(memory_count);
		for (auto& m: plan.memory_info) {
				m.kind                     = r.ReadEnum<IR::ResourceKind>();
				m.resource                 = r.ReadU32();
				m.sampler                  = r.ReadU32();
				m.offset                   = r.ReadU32();
				m.secondary_offset         = r.ReadU32();
				m.dmask                    = r.ReadU32();
				m.data_dwords              = r.ReadU32();
				m.data_bits                = r.ReadU32();
				m.component_index          = r.ReadU32();
				m.component_count           = r.ReadU32();
				m.data_format              = r.ReadU32();
				m.number_format            = r.ReadU32();
				m.image_sample_flags       = r.ReadU32();
				m.image_dimension          = r.ReadEnum<Decoder::ImageDimension>();
				m.image_address_components = r.ReadU32();
				m.address_is_full          = r.ReadBool();
				m.data_signed              = r.ReadBool();
				m.typed                    = r.ReadBool();
				m.formatted                = r.ReadBool();
				m.image_has_mip            = r.ReadBool();
				m.image_r128               = r.ReadBool();
				m.idxen                    = r.ReadBool();
				m.offen                    = r.ReadBool();
				m.coherent                 = r.ReadBool();
				m.planning_only            = r.ReadBool();
		}

		const auto inst_count = r.ReadU32();
		if (!r.Ok() || inst_count > MaxVectorElements) {
				return std::nullopt;
		}
		std::vector<IR::Inst*> insts;
		std::vector<uint32_t>  arg_counts;
		std::vector<bool>      is_phi;
		insts.reserve(inst_count);
		arg_counts.reserve(inst_count);
		is_phi.reserve(inst_count);
		for (uint32_t i = 0; i < inst_count; i++) {
				const auto opcode_bits = r.ReadU32();
				const auto flags        = r.ReadU64();
				const auto argc         = r.ReadU32();
				if (!r.Ok() || opcode_bits >= static_cast<uint32_t>(IR::ValueOpcode::Count) ||
					argc > MaxVectorElements) {
						return std::nullopt;
				}
				const auto opcode = static_cast<IR::ValueOpcode>(opcode_bits);
				const auto arity  = IR::NumArgsOf(opcode);
				const auto phi    = opcode == IR::ValueOpcode::Phi;
				if (!phi && arity != std::numeric_limits<size_t>::max() && argc != arity) {
						return std::nullopt;
				}
				plan.value_storage.emplace_back(opcode, flags);
				insts.push_back(&plan.value_storage.back());
				arg_counts.push_back(argc);
				is_phi.push_back(phi);
		}
		for (uint32_t i = 0; i < inst_count; i++) {
				for (uint32_t a = 0; a < arg_counts[i]; a++) {
						auto value = ReadValue(r, insts);
						if (!value) {
								return std::nullopt;
						}
						if (is_phi[i]) {
								insts[i]->AddPhiOperand(nullptr, *value);
						} else {
								insts[i]->SetArg(a, *value);
						}
				}
		}

		const auto source_count = r.ReadU32();
		if (!r.Ok() || source_count > MaxVectorElements) {
				return std::nullopt;
		}
		plan.descriptor_sources.resize(source_count);
		for (auto& s: plan.descriptor_sources) {
				s.dword_count = r.ReadU32();
				if (!r.Ok() || s.dword_count > 8) {
						return std::nullopt;
				}
				for (uint32_t d = 0; d < s.dword_count; d++) {
						auto value = ReadValue(r, insts);
						if (!value) {
								return std::nullopt;
						}
						s.dwords[d] = *value;
				}
				if (r.ReadBool()) {
						IR::DescriptorSource::IndirectDescriptor indirect;
						if (r.ReadBool()) {
							indirect.selector       = IR::DescriptorSource::IndirectDescriptor::SelectorRead{};
							indirect.selector->source = r.ReadU32();
							indirect.selector->stride = r.ReadU32();
							indirect.selector->offset = r.ReadU32();
						}
						indirect.table_source       = r.ReadU32();
						indirect.table_offset       = r.ReadU32();
						indirect.table_immediate    = r.ReadU32();
						indirect.table_stride       = r.ReadU32();
						indirect.table_record_bytes = r.ReadU32();
						indirect.table_scalar       = r.ReadBool();
						indirect.workgroup_axis     = r.ReadU32();
						auto key_count          = ReadValue(r, insts);
						if (!key_count) {
								return std::nullopt;
						}
						indirect.key_count = *key_count;
						auto selector_first = ReadValue(r, insts);
						if (!selector_first) {
								return std::nullopt;
						}
						indirect.selector_first = *selector_first;
						auto selector_mask = ReadValue(r, insts);
						if (!selector_mask) {
								return std::nullopt;
						}
						indirect.selector_mask = *selector_mask;
						indirect.sources      = r.ReadWords();
						s.indirect_descriptor = std::move(indirect);
				}
		}

		const auto control_flow_count = r.ReadU32();
		if (!r.Ok() || control_flow_count > MaxVectorElements) {
				return std::nullopt;
		}
		plan.control_flow.resize(control_flow_count);
		for (auto& block: plan.control_flow) {
				auto condition = ReadValue(r, insts);
				if (!condition) {
						return std::nullopt;
				}
				block.condition  = *condition;
				block.successors = r.ReadWords();
				block.sources    = r.ReadWords();
		}

		const auto srt_count = r.ReadU32();
		if (!r.Ok() || srt_count > MaxVectorElements) {
				return std::nullopt;
		}
		plan.srt_reads.resize(srt_count);
		for (auto& read: plan.srt_reads) {
				auto value = ReadValue(r, insts);
				if (!value) {
						return std::nullopt;
				}
				read.value       = *value;
				read.flat_offset = r.ReadU32();
		}

		plan.clean_flat_slots = r.ReadBytes();

		auto& fill      = plan.uniform_fill.fill;
		fill.kind       = r.ReadEnum<IR::UniformFillKind>();
		fill.resource   = r.ReadU32();
		for (auto& stride: fill.group_stride) {
				stride = r.ReadU32();
		}
		fill.words = r.ReadU32();
		if (!r.Ok() || fill.words > 4) {
				return std::nullopt;
		}
		fill.value = r.ReadU32();
		for (uint32_t i = 0; i < fill.words; i++) {
				auto value = ReadValue(r, insts);
				if (!value) {
						return std::nullopt;
				}
				plan.uniform_fill.values[i] = *value;
		}

		if (!r.Ok()) {
				return std::nullopt;
		}
		return plan;
}

// ------------------------------------------------------------ ProgramBody ---

std::optional<LoadedProgram> ReadProgramBody(Reader& r) {
		LoadedProgram loaded;

		auto plan = ReadResourcePlan(r);
		if (!plan) {
				return std::nullopt;
		}
		loaded.plan          = std::move(*plan);
		loaded.skip_dispatch = r.ReadBool();

		const auto count = r.ReadU32();
		if (!r.Ok() || count > MaxVectorElements) {
				return std::nullopt;
		}
		loaded.permutations.resize(count);
		for (auto& permutation: loaded.permutations) {
				auto spec = ReadResourceSpecialization(r);
				if (!spec) {
						return std::nullopt;
				}
				permutation.specialization = std::move(*spec);

				auto program = ReadCompiledShaderInfo(r);
				if (!program) {
						return std::nullopt;
				}
				permutation.program = std::move(*program);

				permutation.spirv = r.ReadWords();
		}

		if (!r.Ok()) {
				return std::nullopt;
		}

		// SPIR-V sanity: non-empty with a recognized magic word.
		for (const auto& permutation: loaded.permutations) {
				if (permutation.spirv.empty() || permutation.spirv.front() != 0x07230203u) {
						return std::nullopt;
				}
		}
		return loaded;
}

// --------------------------------------------------------------- Index -----

bool ShaderDiskIndex::Open(const std::filesystem::path& path, std::string_view signature) {
		m_entries.clear();
		m_bytes.clear();

		if (!Common::File::IsFileExisting(path)) {
				return true; // fresh cache
		}

		Common::File file(path, Common::File::Mode::Read);
		if (file.IsInvalid()) {
				return false;
		}
		auto bytes = file.ReadWholeBuffer();
		if (bytes.size() > MaxFileSize) {
				return false;
		}
		m_bytes.assign(reinterpret_cast<const uint8_t*>(bytes.data()),
					   reinterpret_cast<const uint8_t*>(bytes.data()) + bytes.size());

		Reader r{std::span<const uint8_t>(m_bytes)};
		if (r.ReadU64() != Magic || r.ReadU32() != FormatVersion) {
				m_bytes.clear();
				return false;
		}
		if (r.ReadString() != signature) {
				m_bytes.clear();
				return false;
		}

		const auto count = r.ReadU32();
		if (!r.Ok() || count > MaxVectorElements) {
				m_bytes.clear();
				return false;
		}
		for (uint32_t i = 0; i < count; i++) {
				Key key;
				key.stage           = r.ReadEnum<ShaderType>();
				key.hash            = r.ReadU64();
				key.user_data_count = r.ReadU32();
				key.code_size       = r.ReadU32();
				key.static_state    = r.ReadWords();
				const auto offset   = r.ReadU64();
				const auto size     = r.ReadU64();
				if (!r.Ok() || offset > m_bytes.size() || size > m_bytes.size() - offset) {
						m_bytes.clear();
						return false;
				}
				m_entries.emplace(std::move(key), std::make_pair(static_cast<size_t>(offset),
																 static_cast<size_t>(size)));
		}
		if (!r.Ok()) {
				m_bytes.clear();
				return false;
		}
		return true;
}

bool ShaderDiskIndex::Lookup(const Key& key, Reader& body) {
		const auto it = m_entries.find(key);
		if (it == m_entries.end()) {
				return false;
		}
		const auto [offset, size] = it->second;
		body = Reader(std::span<const uint8_t>(m_bytes).subspan(offset, size));
		return true;
}

void ShaderDiskIndex::ForEach(
	const std::function<void(const Key&, std::span<const uint8_t>)>& fn) const {
		for (const auto& [key, position]: m_entries) {
				const auto [offset, size] = position;
				fn(key, std::span<const uint8_t>(m_bytes).subspan(offset, size));
		}
}

} // namespace Libs::Graphics::ShaderDiskCache
