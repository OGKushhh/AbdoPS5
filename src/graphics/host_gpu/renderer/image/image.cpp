#include "graphics/host_gpu/renderer/image/image.h"

#include "common/assert.h"
#include "common/profiler.h"
#include "graphics/host_gpu/renderer/cache/streamBuffer.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/image/imageView.h"
#include "graphics/host_gpu/renderer/renderTarget.h"
#include "kernel/memory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <xxhash.h>

namespace Libs::Graphics {

namespace {

[[nodiscard]] vk::ImageType HostImageType(Prospero::ImageType type) {
	switch (type) {
		case Prospero::ImageType::kColor1D: return vk::ImageType::e1D;
		case Prospero::ImageType::kColor3D: return vk::ImageType::e3D;
		case Prospero::ImageType::kColor2D: return vk::ImageType::e2D;
		default: EXIT("non-base image type: %u\n", static_cast<uint32_t>(type));
	}
}

[[nodiscard]] vk::ImageCreateFlags ImageCreateFlags(const GraphicContext& graphics,
                                                   const ImageInfo& info) {
	vk::ImageCreateFlags flags {};
	if (DepthAspectTransferFormat(info.pixel_format) == vk::Format::eUndefined) {
		flags |= vk::ImageCreateFlagBits::eMutableFormat;
		flags |= vk::ImageCreateFlagBits::eExtendedUsage;
		if (info.IsBlock() && graphics.supports_block_texel_view) {
			flags |= vk::ImageCreateFlagBits::eBlockTexelViewCompatible;
		}
	}
	if (info.IsVolume()) {
		flags |= vk::ImageCreateFlagBits::e2DArrayCompatible;
	}
	return flags;
}

[[nodiscard]] bool HasFormatFeature(vk::FormatProperties      properties,
                                    vk::FormatFeatureFlagBits feature) {
	return static_cast<bool>(properties.optimalTilingFeatures & feature);
}

[[nodiscard]] vk::ImageUsageFlags ImageUsageFlags(GraphicContext& graphics, const ImageInfo& info) {
	if (info.IsBlock()) {
		return vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
		       vk::ImageUsageFlagBits::eSampled;
	}
	const auto properties = graphics.GetFormatProperties(info.pixel_format);
	auto       usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
	if (HasFormatFeature(properties, vk::FormatFeatureFlagBits::eSampledImage)) {
		usage |= vk::ImageUsageFlagBits::eSampled;
	}
	if (DepthAspectTransferFormat(info.pixel_format) != vk::Format::eUndefined) {
		usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
		if (graphics.attachment_feedback_loop_enabled && (usage & vk::ImageUsageFlagBits::eSampled)) {
			usage |= vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT;
		}
		return usage;
	}
	if (HasFormatFeature(properties, vk::FormatFeatureFlagBits::eColorAttachment)) {
		usage |= vk::ImageUsageFlagBits::eColorAttachment;
	}
	if (info.samples == 1) {
		usage |= vk::ImageUsageFlagBits::eStorage;
	}
	return usage;
}

void ValidateOptionalRange(GuestRange range, const char* name) {
	if (!range.ValidOrEmpty()) {
		EXIT("invalid %s image range: address=0x%016llx size=0x%016llx\n", name,
		     static_cast<unsigned long long>(range.address),
		     static_cast<unsigned long long>(range.size));
	}
}

} // namespace

vk::ImageAspectFlags Image::FullAspectMask(vk::Format format) noexcept {
	switch (format) {
		case vk::Format::eD16Unorm:
		case vk::Format::eX8D24UnormPack32:
		case vk::Format::eD32Sfloat: return vk::ImageAspectFlagBits::eDepth;
		case vk::Format::eS8Uint: return vk::ImageAspectFlagBits::eStencil;
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		default: return vk::ImageAspectFlagBits::eColor;
	}
}

// Kyty-033: aspect-aware accessors. For combined depth+stencil formats the
// stencil aspect reads from the dedicated stencil_* fields; every other format
// uses the primary fields for all aspects.
vk::ImageLayout Image::AspectLayout(const VulkanImageState& state,
                                    vk::ImageAspectFlagBits aspect) noexcept {
	if (aspect == vk::ImageAspectFlagBits::eStencil) {
		return state.stencil_layout;
	}
	return state.layout;
}

vk::AccessFlags2 Image::AspectAccess(const VulkanImageState& state,
                                     vk::ImageAspectFlagBits aspect) noexcept {
	if (aspect == vk::ImageAspectFlagBits::eStencil) {
		return state.stencil_access;
	}
	return state.access_mask;
}

vk::PipelineStageFlags2 Image::AspectStage(const VulkanImageState& state,
                                            vk::ImageAspectFlagBits aspect) noexcept {
	if (aspect == vk::ImageAspectFlagBits::eStencil) {
		return state.stencil_pl_stage;
	}
	return state.pl_stage;
}

void Image::SetAspectState(VulkanImageState& state, vk::ImageAspectFlagBits aspect,
                           vk::PipelineStageFlags2 stage, vk::AccessFlags2 access,
                           vk::ImageLayout layout) noexcept {
	if (aspect == vk::ImageAspectFlagBits::eStencil) {
		state.stencil_pl_stage = stage;
		state.stencil_access   = access;
		state.stencil_layout   = layout;
		return;
	}
	state.pl_stage    = stage;
	state.access_mask = access;
	state.layout      = layout;
}

// Kyty-033: validate that the destination layout is compatible with the image's
// usage flags. Returns the aspect mask that the layout can be used with
// (which may be a subset of FullAspectMask for D+S images), or exits if the
// layout is unsupported by the image's usage flags.
vk::ImageAspectFlags Image::ValidateDestinationLayout(vk::ImageLayout layout,
                                                      vk::ImageUsageFlags usage,
                                                      vk::Format format) noexcept {
	const auto full_aspects = FullAspectMask(format);
	switch (layout) {
		case vk::ImageLayout::eUndefined:
		case vk::ImageLayout::eGeneral:
		case vk::ImageLayout::ePreinitialized:
			return full_aspects;
		case vk::ImageLayout::eColorAttachmentOptimal:
			if (!static_cast<bool>(usage & vk::ImageUsageFlagBits::eColorAttachment)) {
				EXIT("Image::GetBarriers: eColorAttachmentOptimal requires eColorAttachment "
				     "usage (usage=0x%x format=%d)\n",
				     static_cast<vk::ImageUsageFlags::MaskType>(usage),
				     static_cast<int>(format));
			}
			return vk::ImageAspectFlagBits::eColor;
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			if (!static_cast<bool>(usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)) {
				EXIT("Image::GetBarriers: depth-stencil layout requires eDepthStencilAttachment "
				     "usage (usage=0x%x format=%d)\n",
				     static_cast<vk::ImageUsageFlags::MaskType>(usage),
				     static_cast<int>(format));
			}
			return full_aspects & (vk::ImageAspectFlagBits::eDepth |
				                       vk::ImageAspectFlagBits::eStencil);
		case vk::ImageLayout::eDepthAttachmentOptimal:
		case vk::ImageLayout::eDepthReadOnlyOptimal:
			if (!static_cast<bool>(usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)) {
				EXIT("Image::GetBarriers: depth-only layout requires eDepthStencilAttachment "
				     "usage (usage=0x%x format=%d)\n",
				     static_cast<vk::ImageUsageFlags::MaskType>(usage),
				     static_cast<int>(format));
			}
			return vk::ImageAspectFlagBits::eDepth;
		case vk::ImageLayout::eStencilAttachmentOptimal:
		case vk::ImageLayout::eStencilReadOnlyOptimal:
			if (!static_cast<bool>(usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)) {
				EXIT("Image::GetBarriers: stencil-only layout requires eDepthStencilAttachment "
				     "usage (usage=0x%x format=%d)\n",
				     static_cast<vk::ImageUsageFlags::MaskType>(usage),
				     static_cast<int>(format));
			}
			return vk::ImageAspectFlagBits::eStencil;
		case vk::ImageLayout::eShaderReadOnlyOptimal:
			if (!static_cast<bool>(usage & vk::ImageUsageFlagBits::eSampled)) {
				EXIT("Image::GetBarriers: eShaderReadOnlyOptimal requires eSampled "
				     "usage (usage=0x%x format=%d)\n",
				     static_cast<vk::ImageUsageFlags::MaskType>(usage),
				     static_cast<int>(format));
			}
			return full_aspects;
		case vk::ImageLayout::eTransferSrcOptimal:
			if (!static_cast<bool>(usage & vk::ImageUsageFlagBits::eTransferSrc)) {
				EXIT("Image::GetBarriers: eTransferSrcOptimal requires eTransferSrc "
				     "usage (usage=0x%x format=%d)\n",
				     static_cast<vk::ImageUsageFlags::MaskType>(usage),
				     static_cast<int>(format));
			}
			return full_aspects;
		case vk::ImageLayout::eTransferDstOptimal:
			if (!static_cast<bool>(usage & vk::ImageUsageFlagBits::eTransferDst)) {
				EXIT("Image::GetBarriers: eTransferDstOptimal requires eTransferDst "
				     "usage (usage=0x%x format=%d)\n",
				     static_cast<vk::ImageUsageFlags::MaskType>(usage),
				     static_cast<int>(format));
			}
			return full_aspects;
		case vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT:
			return full_aspects;
		default:
			return full_aspects;
	}
}

namespace {

// Kyty-033: state bundle used by the coalescing pass. Two pending transitions
// with the same old/new (stage, access, layout) tuple can be merged into a
// single VkImageMemoryBarrier2 with a wider subresourceRange.
struct PendingTransition {
	uint32_t                  level     = 0;
	uint32_t                  layer     = 0;
	vk::PipelineStageFlags2   src_stage;
	vk::AccessFlags2          src_access;
	vk::ImageLayout           src_layout = vk::ImageLayout::eUndefined;
	vk::PipelineStageFlags2   dst_stage;
	vk::AccessFlags2          dst_access;
	vk::ImageLayout           dst_layout = vk::ImageLayout::eUndefined;
};

// Returns true if two pending transitions can be merged into a single barrier
// (i.e. they have identical src/dst stage/access/layout).
[[nodiscard]] bool Mergeable(const PendingTransition& a,
                             const PendingTransition& b) noexcept {
	return a.src_stage == b.src_stage && a.src_access == b.src_access &&
		       a.src_layout == b.src_layout && a.dst_stage == b.dst_stage &&
		       a.dst_access == b.dst_access && a.dst_layout == b.dst_layout;
}

void EmitBarrier(std::vector<vk::ImageMemoryBarrier2>& barriers,
                const PendingTransition& t, uint32_t level_count, uint32_t layer_count,
                vk::Image image, vk::ImageAspectFlags aspect_mask) {
	vk::ImageMemoryBarrier2 barrier {};
	barrier.srcStageMask                    = t.src_stage;
	barrier.srcAccessMask                   = t.src_access;
	barrier.dstStageMask                    = t.dst_stage;
	barrier.dstAccessMask                   = t.dst_access;
	barrier.oldLayout                       = t.src_layout;
	barrier.newLayout                       = t.dst_layout;
	barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.image                           = image;
	barrier.subresourceRange.aspectMask     = aspect_mask;
	barrier.subresourceRange.baseMipLevel   = t.level;
	barrier.subresourceRange.levelCount     = level_count;
	barrier.subresourceRange.baseArrayLayer = t.layer;
	barrier.subresourceRange.layerCount     = layer_count;
	barriers.push_back(barrier);
}

// Kyty-033: coalesce a list of per-subresource pending transitions into the
// minimum number of VkImageMemoryBarrier2 entries. Adjacent subresources
// along the layer axis (within the same mip level) are merged into a single
// barrier with layerCount > 1. Cross-level coalescing is not attempted
// because the straightforward layer-axis coalescing already collapses the
// common case (one barrier per mip level instead of one barrier per
// (level, layer)).
void CoalesceAndEmit(std::vector<vk::ImageMemoryBarrier2>& barriers,
                    std::vector<PendingTransition>& pending, vk::Image image,
                    vk::ImageAspectFlags aspect_mask) {
	if (pending.empty()) {
		return;
	}
	// Sort by (level, layer) so adjacent subresources are contiguous.
	std::sort(pending.begin(), pending.end(),
		          [](const PendingTransition& a, const PendingTransition& b) {
			          if (a.level != b.level) return a.level < b.level;
			          return a.layer < b.layer;
		          });

	auto run_start = pending.begin();
	auto it        = run_start;
	while (it != pending.end()) {
		auto next = std::next(it);
		// Try to extend the current run along the layer axis.
		if (next != pending.end() && next->level == it->level &&
			    next->layer == it->layer + 1 && Mergeable(*run_start, *next)) {
			it = next;
			continue;
		}
		// Run ends here. Emit one barrier spanning [run_start, it].
		const auto base        = *run_start;
		const auto run_layers  = it->layer - run_start->layer + 1;
		EmitBarrier(barriers, base, 1u, run_layers, image, aspect_mask);
		run_start = next;
		it        = next;
	}
}

} // namespace

Image::Barriers Image::GetBarriers(vk::ImageLayout                      destination_layout,
                                   vk::AccessFlags2                     destination_access,
                                   vk::PipelineStageFlags2              destination_stage,
                                   std::optional<ImageSubresourceRange> range) {
	auto& state              = backing.state;
	auto& subresource_states = backing.subresource_states;
	if (range && info.IsVolume()) {
		range->base_layer  = 0;
		range->layer_count = 1;
	}

	// Kyty-033: validate the destination layout up-front. This catches
	// programming errors (e.g. transitioning a sampled-only image to
	// eColorAttachmentOptimal) that would otherwise surface as a Vulkan
	// validation error at draw time, far from the root cause.
	(void)ValidateDestinationLayout(destination_layout, backing.usage, backing.format);

	const bool partial =
	    range && (range->base_level != 0 || range->level_count != info.resources.levels ||
	              range->base_layer != 0 || range->layer_count != info.resources.layers);
	const bool has_subresource_states = !subresource_states.empty();

	// Kyty-033: a write-access barrier must be re-emitted even when the
	// layout doesn't change, because the previous access may not yet be
	// visible to the new pipeline stage. We extend the write mask with
	// eColorAttachmentWrite and eDepthStencilAttachmentWrite so render
	// passes with consecutive writes to the same target also sync correctly.
	constexpr auto write_access = vk::AccessFlagBits2::eTransferWrite |
		                              vk::AccessFlagBits2::eShaderWrite |
		                              vk::AccessFlagBits2::eMemoryWrite |
		                              vk::AccessFlagBits2::eColorAttachmentWrite |
		                              vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

	Barriers barriers;
	if (partial || has_subresource_states) {
		if (!has_subresource_states) {
			subresource_states.resize(info.resources.levels * info.resources.layers, state);
		}

		const uint32_t base_level  = partial ? range->base_level : 0;
		const uint32_t level_count = partial ? range->level_count : info.resources.levels;
		const uint32_t base_layer  = partial ? range->base_layer : 0;
		const uint32_t layer_count = partial ? range->layer_count : info.resources.layers;
		std::vector<PendingTransition> pending;
		pending.reserve(level_count * layer_count);
		for (uint32_t level = base_level; level < base_level + level_count; level++) {
			for (uint32_t layer = base_layer; layer < base_layer + layer_count; layer++) {
				const auto index = level * info.resources.layers + layer;
				EXIT_IF(index >= subresource_states.size());
				auto& subresource_state = subresource_states[index];

				const bool repeated_write =
					    static_cast<bool>(subresource_state.access_mask & write_access);
				if (subresource_state.layout != destination_layout ||
					    subresource_state.access_mask != destination_access || repeated_write) {
					PendingTransition t;
					t.level      = level;
					t.layer      = layer;
					t.src_stage  = subresource_state.pl_stage;
					t.src_access = subresource_state.access_mask;
					t.src_layout = subresource_state.layout;
					t.dst_stage  = destination_stage;
					t.dst_access = destination_access;
					t.dst_layout = destination_layout;
					pending.push_back(t);
					// Kyty-033: update only the aspect(s) the destination layout
					// applies to. For depth-only layouts the stencil aspect
					// retains its previous state; for stencil-only layouts the
					// depth (primary) aspect retains its previous state. For all
					// other layouts both aspects are updated together, matching
					// the pre-Kyty-033 behavior.
					const bool depth_only =
						    destination_layout == vk::ImageLayout::eDepthAttachmentOptimal ||
						    destination_layout == vk::ImageLayout::eDepthReadOnlyOptimal;
					const bool stencil_only =
						    destination_layout == vk::ImageLayout::eStencilAttachmentOptimal ||
						    destination_layout == vk::ImageLayout::eStencilReadOnlyOptimal;
					if (!stencil_only) {
						SetAspectState(subresource_state,
							               vk::ImageAspectFlagBits::eColor,
							               destination_stage, destination_access,
							               destination_layout);
					}
					if (!depth_only) {
						SetAspectState(subresource_state,
							               vk::ImageAspectFlagBits::eStencil,
							               destination_stage, destination_access,
							               destination_layout);
					}
				}
			}
		}

		// Kyty-033: coalesce adjacent subresources with the same transition
		// into a single barrier with layerCount > 1. This significantly
		// reduces the number of vkCmdPipelineBarrier2 entries for textures
		// with many array layers (e.g. cubemaps, cascade shadow maps).
		CoalesceAndEmit(barriers, pending, backing.image,
			                FullAspectMask(backing.format));

		if (!partial) {
			subresource_states.clear();
		}
	} else {
		const bool repeated_write = static_cast<bool>(state.access_mask & write_access);
		if (state.layout == destination_layout && state.access_mask == destination_access &&
		    !repeated_write) {
			return {};
		}

		vk::ImageMemoryBarrier2 barrier {};
		barrier.srcStageMask                    = state.pl_stage;
		barrier.srcAccessMask                   = state.access_mask;
		barrier.dstStageMask                    = destination_stage;
		barrier.dstAccessMask                   = destination_access;
		barrier.oldLayout                       = state.layout;
		barrier.newLayout                       = destination_layout;
		barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
		barrier.image                           = backing.image;
		barrier.subresourceRange.aspectMask     = FullAspectMask(backing.format);
		barrier.subresourceRange.baseMipLevel   = 0;
		barrier.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;
		barriers.push_back(barrier);
	}

	// Kyty-033: update the whole-image state for the appropriate aspect(s).
	// For depth-only layouts, only the depth (primary) aspect is updated and
	// the stencil aspect retains its previous state. For stencil-only layouts,
	// only the stencil aspect is updated and the depth (primary) aspect
	// retains its previous state. For all other layouts, both aspects are
	// kept in sync (matching the previous behavior).
	const bool whole_depth_only =
		    destination_layout == vk::ImageLayout::eDepthAttachmentOptimal ||
		    destination_layout == vk::ImageLayout::eDepthReadOnlyOptimal;
	const bool whole_stencil_only =
		    destination_layout == vk::ImageLayout::eStencilAttachmentOptimal ||
		    destination_layout == vk::ImageLayout::eStencilReadOnlyOptimal;
	if (!whole_stencil_only) {
		state.pl_stage    = destination_stage;
		state.access_mask = destination_access;
		state.layout      = destination_layout;
	}
	if (!whole_depth_only) {
		state.stencil_pl_stage = destination_stage;
		state.stencil_access   = destination_access;
		state.stencil_layout   = destination_layout;
	}
	return barriers;
}

void Image::Transit(vk::ImageLayout destination_layout, vk::AccessFlags2 destination_access,
                    std::optional<ImageSubresourceRange> range, vk::CommandBuffer command_buffer) {
	const auto transfer_access =
	    vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
	vk::PipelineStageFlags2 destination_stage {};
	if (static_cast<bool>(destination_access & transfer_access)) {
		destination_stage |= vk::PipelineStageFlagBits2::eTransfer;
	}
	if (!destination_access ||
	    static_cast<bool>(destination_access & ~vk::AccessFlags2 {transfer_access})) {
		destination_stage |=
		    vk::PipelineStageFlagBits2::eAllGraphics | vk::PipelineStageFlagBits2::eComputeShader;
	}
	const auto barriers =
	    GetBarriers(destination_layout, destination_access, destination_stage, range);
	if (barriers.empty()) {
		return;
	}
	m_scheduler.EndRendering();
	vk::DependencyInfo dependency {};
	dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
	dependency.pImageMemoryBarriers    = barriers.data();
	command_buffer.pipelineBarrier2(dependency);
}

void Image::Upload(std::span<const vk::BufferImageCopy> copies, vk::Buffer buffer, uint64_t offset,
                   uint64_t size) {
	EXIT_IF(copies.empty() || buffer == nullptr || size == 0);
	m_scheduler.EndRendering();
	vk::BufferMemoryBarrier2 buffer_barrier {};
	buffer_barrier.srcStageMask        = vk::PipelineStageFlagBits2::eAllCommands;
	buffer_barrier.srcAccessMask       = vk::AccessFlagBits2::eMemoryWrite;
	buffer_barrier.dstStageMask        = vk::PipelineStageFlagBits2::eTransfer;
	buffer_barrier.dstAccessMask       = vk::AccessFlagBits2::eTransferRead;
	buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.buffer              = buffer;
	buffer_barrier.offset              = offset;
	buffer_barrier.size                = size;
	const auto image_barriers =
	    GetBarriers(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite,
	                vk::PipelineStageFlagBits2::eCopy, {});
	vk::DependencyInfo dependency {};
	dependency.dependencyFlags          = vk::DependencyFlagBits::eByRegion;
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers    = &buffer_barrier;
	dependency.imageMemoryBarrierCount  = static_cast<uint32_t>(image_barriers.size());
	dependency.pImageMemoryBarriers     = image_barriers.data();
	auto command                        = m_scheduler.Current().Handle();
	command.pipelineBarrier2(dependency);
	command.copyBufferToImage(buffer, backing.image, vk::ImageLayout::eTransferDstOptimal,
	                          static_cast<uint32_t>(copies.size()), copies.data());
	buffer_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
	buffer_barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
	buffer_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	buffer_barrier.dstAccessMask =
	    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
	dependency.imageMemoryBarrierCount = 0;
	dependency.pImageMemoryBarriers    = nullptr;
	command.pipelineBarrier2(dependency);
	Transit(vk::ImageLayout::eGeneral,
	        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead, {}, command);
}

void Image::Download(std::span<const vk::BufferImageCopy> copies, vk::Buffer buffer,
                     uint64_t offset, uint64_t size) {
	EXIT_IF(copies.empty() || buffer == nullptr || size == 0);
	m_scheduler.EndRendering();
	vk::BufferMemoryBarrier2 buffer_barrier {};
	buffer_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
	buffer_barrier.srcAccessMask =
	    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
	buffer_barrier.dstStageMask        = vk::PipelineStageFlagBits2::eCopy;
	buffer_barrier.dstAccessMask       = vk::AccessFlagBits2::eTransferWrite;
	buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.buffer              = buffer;
	buffer_barrier.offset              = offset;
	buffer_barrier.size                = size;
	const auto image_barriers =
	    GetBarriers(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead,
	                vk::PipelineStageFlagBits2::eCopy, {});
	vk::DependencyInfo dependency {};
	dependency.dependencyFlags          = vk::DependencyFlagBits::eByRegion;
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers    = &buffer_barrier;
	dependency.imageMemoryBarrierCount  = static_cast<uint32_t>(image_barriers.size());
	dependency.pImageMemoryBarriers     = image_barriers.data();
	auto command                        = m_scheduler.Current().Handle();
	command.pipelineBarrier2(dependency);
	command.copyImageToBuffer(backing.image, vk::ImageLayout::eTransferSrcOptimal, buffer,
	                          static_cast<uint32_t>(copies.size()), copies.data());
	buffer_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eCopy;
	buffer_barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
	buffer_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	buffer_barrier.dstAccessMask =
	    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
	dependency.imageMemoryBarrierCount = 0;
	dependency.pImageMemoryBarriers    = nullptr;
	command.pipelineBarrier2(dependency);
}

std::pair<uint32_t, uint32_t> Image::SanitizeCopyLayers(const Image& source,
                                                        const Image& destination, uint32_t depth) {
	const auto source_type        = source.backing.image_type;
	const auto destination_type   = destination.backing.image_type;
	uint32_t   source_layers      = source.backing.layers;
	uint32_t   destination_layers = destination.backing.layers;
	if (source_type == vk::ImageType::e3D) {
		source_layers = 1;
	}
	if (destination_type == vk::ImageType::e3D) {
		destination_layers = 1;
	}
	if (source_type == destination_type) {
		source_layers = destination_layers = std::min(source_layers, destination_layers);
	} else if (source_type == vk::ImageType::e2D && destination_type == vk::ImageType::e3D) {
		source_layers = depth;
	} else if (source_type == vk::ImageType::e3D && destination_type == vk::ImageType::e2D) {
		destination_layers = depth;
	}
	return {source_layers, destination_layers};
}

void Image::CopyImage(Image& source) {
	EXIT_IF(source.backing.samples != backing.samples);
	m_scheduler.EndRendering();
	const uint32_t levels     = std::min(source.backing.mip_levels, backing.mip_levels);
	const uint32_t base_depth = backing.image_type == vk::ImageType::e3D
	                                ? backing.extent.depth
	                                : source.backing.extent.depth;
	const auto     source_aspect =
	    FullAspectMask(source.backing.format) & ~vk::ImageAspectFlagBits::eStencil;
	const auto destination_aspect =
	    FullAspectMask(backing.format) & ~vk::ImageAspectFlagBits::eStencil;
	std::vector<vk::ImageCopy> copies;
	copies.reserve(levels);
	for (uint32_t level = 0; level < levels; level++) {
		const auto width  = std::max(source.backing.extent.width >> level, 1u);
		const auto height = std::max(source.backing.extent.height >> level, 1u);
		const auto depth  = std::max(base_depth >> level, 1u);
		const auto [source_layers, destination_layers] = SanitizeCopyLayers(source, *this, depth);
		vk::ImageCopy copy {};
		copy.srcSubresource = {source_aspect, level, 0, 1};
		copy.dstSubresource = {destination_aspect, level, 0, 1};
		if (source.backing.image_type == backing.image_type) {
			if (source.backing.image_type == vk::ImageType::e3D) {
				copy.extent = {width, height, depth};
			} else {
				copy.srcSubresource.layerCount = std::min(source_layers, destination_layers);
				copy.dstSubresource.layerCount = copy.srcSubresource.layerCount;
				copy.extent                    = {width, height, 1};
			}
		} else if (source.backing.image_type == vk::ImageType::e2D) {
			copy.srcSubresource.layerCount = source_layers;
			copy.extent                    = {width, height, source_layers};
		} else {
			copy.dstSubresource.layerCount = destination_layers;
			copy.extent                    = {width, height, destination_layers};
		}
		copies.push_back(copy);
	}
	if (copies.empty()) {
		return;
	}
	auto command = m_scheduler.Current().Handle();
	source.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, {},
	               command);
	Transit(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, {}, command);
	command.copyImage(source.backing.image, vk::ImageLayout::eTransferSrcOptimal, backing.image,
	                  vk::ImageLayout::eTransferDstOptimal, static_cast<uint32_t>(copies.size()),
	                  copies.data());
	Transit(vk::ImageLayout::eGeneral,
	        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead, {}, command);
}

void Image::Resolve(Image& source, const ImageSubresourceRange& source_range,
                    const ImageSubresourceRange& destination_range) {
	EXIT_IF(backing.samples != 1 || source.backing.image_type != vk::ImageType::e2D ||
	        backing.image_type != vk::ImageType::e2D || source_range.level_count != 1 ||
	        destination_range.level_count != 1 ||
	        source_range.base_level >= source.backing.mip_levels ||
	        destination_range.base_level >= backing.mip_levels ||
	        source_range.base_layer >= source.backing.layers ||
	        destination_range.base_layer >= backing.layers);
	const auto layers       = std::min({source_range.layer_count, destination_range.layer_count,
	                                    source.backing.layers - source_range.base_layer,
	                                    backing.layers - destination_range.base_layer});
	const auto source_width = std::max(source.backing.extent.width >> source_range.base_level, 1u);
	const auto source_height =
	    std::max(source.backing.extent.height >> source_range.base_level, 1u);
	const auto destination_width =
	    std::max(backing.extent.width >> destination_range.base_level, 1u);
	const auto destination_height =
	    std::max(backing.extent.height >> destination_range.base_level, 1u);
	const bool copy = source.backing.samples == 1;
	EXIT_IF(layers == 0 || info.extent.width > source_width || info.extent.height > source_height ||
	        info.extent.width > destination_width || info.extent.height > destination_height ||
	        (copy ? !ImageViewOps::FormatsCompatible(source.backing.format, backing.format)
	              : source.backing.format != backing.format));
	auto resolved_source_range             = source_range;
	auto resolved_destination_range        = destination_range;
	resolved_source_range.layer_count      = layers;
	resolved_destination_range.layer_count = layers;
	const vk::Extent3D resolve_extent {info.extent.width, info.extent.height, 1};

	m_scheduler.EndRendering();
	auto command = m_scheduler.Current().Handle();
	source.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead,
	               resolved_source_range, command);
	Transit(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite,
	        resolved_destination_range, command);
	if (copy) {
		vk::ImageCopy region {};
		region.srcSubresource = {vk::ImageAspectFlagBits::eColor, resolved_source_range.base_level,
		                         resolved_source_range.base_layer, layers};
		region.dstSubresource = {vk::ImageAspectFlagBits::eColor,
		                         resolved_destination_range.base_level,
		                         resolved_destination_range.base_layer, layers};
		region.extent         = resolve_extent;
		command.copyImage(source.backing.image, vk::ImageLayout::eTransferSrcOptimal, backing.image,
		                  vk::ImageLayout::eTransferDstOptimal, region);
	} else {
		vk::ImageResolve region {};
		region.srcSubresource = {vk::ImageAspectFlagBits::eColor, resolved_source_range.base_level,
		                         resolved_source_range.base_layer, layers};
		region.dstSubresource = {vk::ImageAspectFlagBits::eColor,
		                         resolved_destination_range.base_level,
		                         resolved_destination_range.base_layer, layers};
		region.extent         = resolve_extent;
		command.resolveImage(source.backing.image, vk::ImageLayout::eTransferSrcOptimal,
		                     backing.image, vk::ImageLayout::eTransferDstOptimal, region);
	}
}

uint32_t Image::CopyRows(uint64_t row_size, uint32_t rows, uint64_t capacity) noexcept {
	if (row_size == 0 || rows == 0 || row_size > capacity) {
		return 0;
	}
	return static_cast<uint32_t>(std::min<uint64_t>(rows, capacity / row_size));
}

void Image::CopyImageWithBuffer(Image& source, Buffer& buffer) {
	EXIT_IF(buffer.Handle() == nullptr || source.backing.samples != 1 || backing.samples != 1);
	m_scheduler.EndRendering();
	const uint32_t levels = std::min(source.backing.mip_levels, backing.mip_levels);
	const auto     source_aspect =
	    FullAspectMask(source.backing.format) & ~vk::ImageAspectFlagBits::eStencil;
	const auto destination_aspect =
	    FullAspectMask(backing.format) & ~vk::ImageAspectFlagBits::eStencil;
	const auto     source_bytes      = DepthAspectTransferBytes(source.backing.format) != 0
	                                       ? DepthAspectTransferBytes(source.backing.format)
	                                       : source.info.bytes_per_block;
	const auto     destination_bytes = DepthAspectTransferBytes(backing.format) != 0
	                                       ? DepthAspectTransferBytes(backing.format)
	                                       : info.bytes_per_block;
	const uint32_t source_block      = source.info.IsBlock() ? 4u : 1u;
	const uint32_t destination_block = info.IsBlock() ? 4u : 1u;
	EXIT_IF(levels == 0 || source_bytes == 0 || source_bytes != destination_bytes ||
	        source_block != destination_block);

	vk::BufferMemoryBarrier2 barrier {};
	barrier.srcStageMask        = vk::PipelineStageFlagBits2::eTransfer;
	barrier.srcAccessMask       = vk::AccessFlagBits2::eTransferRead;
	barrier.dstStageMask        = vk::PipelineStageFlagBits2::eTransfer;
	barrier.dstAccessMask       = vk::AccessFlagBits2::eTransferWrite;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer              = buffer.Handle();
	barrier.offset              = 0;
	vk::DependencyInfo dependency {};
	dependency.dependencyFlags          = vk::DependencyFlagBits::eByRegion;
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers    = &barrier;
	auto command                        = m_scheduler.Current().Handle();
	source.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, {},
	               command);
	Transit(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, {}, command);
	for (uint32_t level = 0; level < levels; level++) {
		const auto width             = std::max(source.backing.extent.width >> level, 1u);
		const auto height            = std::max(source.backing.extent.height >> level, 1u);
		const auto source_depth      = source.backing.image_type == vk::ImageType::e3D
		                                   ? std::max(source.backing.extent.depth >> level, 1u)
		                                   : source.backing.layers;
		const auto destination_depth = backing.image_type == vk::ImageType::e3D
		                                   ? std::max(backing.extent.depth >> level, 1u)
		                                   : backing.layers;
		const auto slices            = std::min(source_depth, destination_depth);
		const auto block_rows        = (height + source_block - 1) / source_block;
		const auto row_size =
		    static_cast<uint64_t>((width + source_block - 1) / source_block) * source_bytes;
		const auto rows_per_copy = CopyRows(row_size, block_rows, buffer.Size());
		EXIT_IF(slices == 0 || rows_per_copy == 0);
		for (uint32_t slice = 0; slice < slices; slice++) {
			for (uint32_t block_row = 0; block_row < block_rows; block_row += rows_per_copy) {
				const auto          copy_rows   = std::min(rows_per_copy, block_rows - block_row);
				const auto          y           = block_row * source_block;
				const auto          copy_height = std::min(copy_rows * source_block, height - y);
				const auto          copy_size   = row_size * copy_rows;
				vk::BufferImageCopy source_copy {};
				source_copy.imageSubresource = {
				    source_aspect, level,
				    source.backing.image_type == vk::ImageType::e3D ? 0u : slice, 1};
				source_copy.imageOffset           = {0, static_cast<int32_t>(y),
				                                     source.backing.image_type == vk::ImageType::e3D
				                                         ? static_cast<int32_t>(slice)
				                                         : 0};
				source_copy.imageExtent           = {width, copy_height, 1};
				auto destination_copy             = source_copy;
				destination_copy.imageSubresource = {
				    destination_aspect, level,
				    backing.image_type == vk::ImageType::e3D ? 0u : slice, 1};
				destination_copy.imageOffset.z =
				    backing.image_type == vk::ImageType::e3D ? static_cast<int32_t>(slice) : 0;
				barrier.size          = copy_size;
				barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
				barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
				command.pipelineBarrier2(dependency);
				command.copyImageToBuffer(source.backing.image,
				                          vk::ImageLayout::eTransferSrcOptimal, buffer.Handle(),
				                          source_copy);
				barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
				barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
				command.pipelineBarrier2(dependency);
				command.copyBufferToImage(buffer.Handle(), backing.image,
				                          vk::ImageLayout::eTransferDstOptimal, destination_copy);
			}
		}
	}
	Transit(vk::ImageLayout::eGeneral,
	        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead, {}, command);
}

void Image::CopyMip(Image& source, uint32_t mip, uint32_t layer) {
	EXIT_IF(source.backing.samples != backing.samples || mip >= backing.mip_levels ||
	        layer >= backing.layers);
	m_scheduler.EndRendering();
	const auto width  = std::max(backing.extent.width >> mip, 1u);
	const auto height = std::max(backing.extent.height >> mip, 1u);
	const auto depth  = std::max(backing.extent.depth >> mip, 1u);
	EXIT_IF(width != source.backing.extent.width || height != source.backing.extent.height);
	const auto [source_layers, destination_layers] = SanitizeCopyLayers(source, *this, depth);
	const auto aspects                             = FullAspectMask(source.backing.format);
	EXIT_IF(aspects != FullAspectMask(backing.format));
	std::array<vk::ImageCopy, 2> copies {};
	uint32_t                     copy_count = 0;
	for (const auto aspect: {vk::ImageAspectFlagBits::eColor, vk::ImageAspectFlagBits::eDepth,
	                         vk::ImageAspectFlagBits::eStencil}) {
		if (!static_cast<bool>(aspects & aspect)) {
			continue;
		}
		auto& copy          = copies[copy_count++];
		copy.srcSubresource = {aspect, 0, 0, source_layers};
		copy.dstSubresource = {aspect, mip, layer, destination_layers};
		copy.extent         = {width, height, depth};
	}
	auto command = m_scheduler.Current().Handle();
	Transit(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, {}, command);
	source.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, {},
	               command);
	command.copyImage(source.backing.image, vk::ImageLayout::eTransferSrcOptimal, backing.image,
	                  vk::ImageLayout::eTransferDstOptimal, copy_count, copies.data());
	Transit(vk::ImageLayout::eGeneral,
	        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead, {}, command);
}

namespace ImageOps {

void Validate(const ImageInfo& info) {
	ValidateOptionalRange(info.data, "data");
	ValidateOptionalRange(info.stencil, "stencil");

	if (info.pixel_format == vk::Format::eUndefined) {
		const bool metadata_empty =
		    info.metadata.range.Empty() && info.metadata.kind == ImageMetadataKind::None &&
		    info.metadata.control == 0 &&
		    info.metadata.compression == VideoOutCompression::Uncompressed &&
		    !info.metadata.stencil_compressed;
		if (info.data.Empty() || info.HasStencil() || !metadata_empty || info.extent.width == 0 ||
		    info.extent.height == 0 || info.extent.depth == 0 || info.resources.levels != 1 ||
		    info.resources.layers != 1 || info.samples != 1 || info.pitch != 0 ||
		    info.bytes_per_block != 0) {
			EXIT("invalid stencil association image\n");
		}
		return;
	}

	if (info.extent.width == 0 || info.extent.height == 0 || info.extent.depth == 0 ||
	    info.resources.levels == 0 || info.resources.levels > info.mip_layout.size() ||
	    info.resources.layers == 0 || info.samples == 0 ||
	    vulkan_sample_count(info.samples) == vk::SampleCountFlagBits {} ||
	    info.bytes_per_block == 0 || (info.data.address != 0 && info.pitch == 0)) {
		EXIT("invalid image geometry or format\n");
	}

	switch (info.type) {
		case Prospero::ImageType::kColor1D:
			if (info.extent.height != 1 || info.extent.depth != 1) {
				EXIT("invalid 1D image shape\n");
			}
			break;
		case Prospero::ImageType::kColor3D:
			if (info.resources.layers != 1) {
				EXIT("3D images cannot have array layers\n");
			}
			break;
		case Prospero::ImageType::kColor2D:
			if (info.extent.depth != 1) {
				EXIT("invalid 2D image shape\n");
			}
			break;
		default: EXIT("non-base image type: %u\n", static_cast<uint32_t>(info.type));
	}
	if (info.samples > 1 && info.resources.levels != 1) {
		EXIT("multisampled images cannot have mip levels\n");
	}

	if (info.metadata.stencil_compressed && !info.HasStencil()) {
		EXIT("compressed stencil metadata requires a stencil plane\n");
	}
	switch (info.metadata.kind) {
		case ImageMetadataKind::None:
			if (!info.metadata.range.Empty() || info.metadata.control != 0 ||
			    info.metadata.compression != VideoOutCompression::Uncompressed ||
			    info.metadata.stencil_compressed) {
				EXIT("metadata-free image has metadata state\n");
			}
			break;
		case ImageMetadataKind::Htile:
			if (!info.metadata.range.Valid() ||
			    info.metadata.compression != VideoOutCompression::Uncompressed) {
				EXIT("invalid HTILE metadata\n");
			}
			break;
		case ImageMetadataKind::Dcc:
			if (info.metadata.range.address == 0 ||
			    info.metadata.range.address >= TRACKER_ADDRESS_SIZE ||
			    (info.metadata.range.size != 0 &&
			     info.metadata.range.size > TRACKER_ADDRESS_SIZE - info.metadata.range.address) ||
			    info.metadata.compression == VideoOutCompression::Unsupported) {
				EXIT("invalid DCC metadata\n");
			}
			break;
	}
}

Prospero::BufferFormat RenderTargetTransferFormat(uint32_t bytes_per_element) {
	switch (bytes_per_element) {
		case 1: return Prospero::BufferFormat::k8UNorm;
		case 2: return Prospero::BufferFormat::k16UNorm;
		case 4: return Prospero::BufferFormat::k32Float;
		case 8: return Prospero::BufferFormat::k16_16_16_16Float;
		case 16: return Prospero::BufferFormat::k32_32_32_32Float;
		default: EXIT("unsupported render-target element size: %u\n", bytes_per_element);
	}
}

} // namespace ImageOps

Image::Image(GraphicContext& graphics, CommandScheduler& scheduler, const ImageInfo& image_info)
    : info(image_info), m_graphics(graphics), m_scheduler(scheduler) {
	KYTY_PROFILER_FUNCTION();
	ImageOps::Validate(info);
	m_cpu_dirty =
	    !info.data.Empty() && info.metadata.compression == VideoOutCompression::Uncompressed;
	if (info.pixel_format == vk::Format::eUndefined) {
		return;
	}

	vk::ImageCreateInfo create {};
	create.flags         = ImageCreateFlags(graphics, info);
	create.imageType     = HostImageType(info.type);
	create.extent        = info.extent;
	create.mipLevels     = info.resources.levels;
	create.arrayLayers   = info.IsVolume() ? 1u : info.resources.layers;
	create.format        = info.pixel_format;
	create.tiling        = vk::ImageTiling::eOptimal;
	create.initialLayout = vk::ImageLayout::eUndefined;
	create.usage         = ImageUsageFlags(graphics, info);
	create.samples       = vulkan_sample_count(info.samples);

	vk::ImageFormatProperties properties {};
	if (graphics.GetImageFormatProperties(create.format, create.imageType, create.tiling,
	                                      create.usage, create.flags,
	                                      &properties) != vk::Result::eSuccess ||
	    !static_cast<bool>(properties.sampleCounts & create.samples)) {
		EXIT("image format does not support required usage: format=%d type=%d usage=0x%x "
		     "flags=0x%x samples=%u\n",
		     static_cast<int>(create.format), static_cast<int>(create.imageType),
		     static_cast<vk::ImageUsageFlags::MaskType>(create.usage),
		     static_cast<vk::ImageCreateFlags::MaskType>(create.flags), info.samples);
	}

	if (!graphics.CreateImage(create, backing)) {
		EXIT("failed to create image: extent=%ux%ux%u format=%d layers=%u levels=%u\n",
		     create.extent.width, create.extent.height, create.extent.depth,
		     static_cast<int>(create.format), create.arrayLayers, create.mipLevels);
	}
}

uint64_t Image::HashGuestEdges() const {
	std::array<uint8_t, TRACKER_PAGE_SIZE * 2> bytes {};
	const auto                                 range = info.data;
	const uint64_t head_end =
	    std::min(range.End(), Common::AlignUp(range.address, TRACKER_PAGE_SIZE));
	const uint64_t tail_begin =
	    std::max(range.address, Common::AlignDown(range.End(), TRACKER_PAGE_SIZE));
	const uint64_t head_size    = head_end - range.address;
	const uint64_t tail_address = tail_begin < head_end ? head_end : tail_begin;
	const uint64_t tail_size    = range.End() - tail_address;
	if ((head_size != 0 &&
	     !LibKernel::Memory::TryReadBacking(range.address, bytes.data(), head_size)) ||
	    (tail_size != 0 &&
	     !LibKernel::Memory::TryReadBacking(tail_address, bytes.data() + head_size, tail_size))) {
		EXIT("Image: failed to hash guest backing\n");
	}
	return XXH3_64bits(bytes.data(), static_cast<size_t>(head_size + tail_size));
}

Image::~Image() {
	KYTY_PROFILER_FUNCTION();
	for (const auto& cached: views) {
		if (cached.view != nullptr) {
			m_graphics.device.destroyImageView(cached.view, nullptr);
		}
	}
	if (backing.image != nullptr) {
		m_graphics.DeleteImage(backing);
	}
}

} // namespace Libs::Graphics
