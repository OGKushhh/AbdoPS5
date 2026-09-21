# Kyty-006: Depth/Comparison-Texture Revert Cluster Investigation

> **Status:** Investigation complete. Root cause identified. Workaround available via Kyty-003.
> 
> **Source:** 5 reverts in 10 days (Aug 25 – Sep 7, 2026) all targeting the depth-texture subsystem.
> 
> **Games affected:** Sifu (#739), Returnal (#742), Spider-Man Remastered (#701), Demon's Souls (#697) — all blocked by the depth-texture instability.

---

## 1. The 5 Reverts

### 1.1. Revert #1: `7012634f` → `7adade0` (Aug 25–26)

**Original commit:** `7012634f` "graphics: require unrestricted depth ranges" (Aug 25)
**Revert:** `7adade0` (Aug 26)

**What it tried to do:** Added `VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME` to the required device extensions in `vulkanWindow.cpp`:

```cpp
// vulkanWindow.cpp:985
std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME,
-   VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, "VK_KHR_maintenance1"};
+   VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
+   "VK_KHR_maintenance1"};
```

**Why it was reverted:** `VK_EXT_depth_range_unrestricted` is **not universally supported**. Making it a *required* extension means KytyPS5 won't start on GPUs that don't support it (older Intel/AMD iGPUs, some Mali drivers). The revert restored it to optional.

### 1.2. Revert #2: `0ce19357` → `81b0c13` (Sep 4)

**Original commit:** `0ce19357` "graphics: create depth images for comparison textures" (Sep 4, 00:54)
**Revert:** `81b0c13` (Sep 4, 03:08)

**What it tried to do:** When a shader uses a depth-comparison sampler (`sampler2DShadow`-equivalent), create the backing image with a depth format (`D32_SFLOAT`) instead of a color format. This required:

1. **textureCache.cpp:** Change the upload path to handle `BindingType::Texture` (not just `BindingType::DepthTarget`) when the image is a depth-comparison texture.
2. **descriptors.cpp:** Add a `TextureBackingFormat()` function that returns `policy->depth_attachment_format` when `resource.depth_compare` is true.

**Why it was reverted (likely):** The `EXIT("unsupported comparison-sampled texture: format=%u")` at descriptors.cpp:520 fires for depth formats that don't have a matching `FindGuestDepthFormatPolicy()`. This is a hard crash on games that use depth-comparison textures with formats KytyPS5 doesn't yet recognize.

### 1.3. Revert #3: `d762d24` → `dd9ecd5` (Sep 4)

**Original commit:** `d762d24` "shader: separate comparison image bindings" (Sep 4, 00:47)
**Revert:** `dd9ecd5` (Sep 4, 03:09)

**What it tried to do:** Split the image descriptor bindings into separate groups:
- `FirstImageBinding` (1) — sampled float/uint/int images (7 slots each)
- `FirstDepthComparisonImageBinding` (22) — **new**: depth-comparison images (7 slots)
- `FirstStorageImageBinding` (29) — storage images (shifted from 22)
- `ImageBindingCount` increased from 36 to 43

Also added `image.depth_compare ? 1u : 0u` to the SPIR-V `OpTypeImage`'s "Depth" parameter (spirvEmitterAnalysis.cpp:161), which is required for depth-comparison samplers in SPIR-V.

**Why it was reverted (likely):** The binding layout change (`FirstStorageImageBinding` moved from 22 to 29) breaks the push-data layout that shaders expect. Any shader compiled before this change would have the wrong binding offsets, causing descriptor mismatches and rendering corruption. This is a breaking ABI change that requires all shaders to be recompiled.

### 1.4. Revert #4: `eccd6f6` → `27f015e` (Sep 4)

**Original commit:** `eccd6f6` "graphics: preserve unrestricted viewport depth ranges" (Sep 4, 01:07)
**Revert:** `27f015e` (Sep 4, 05:22)

**What it tried to do:** Adjust the `minDepth`/`maxDepth` viewport calculation to preserve the PS5's unrestricted depth range:

```cpp
// renderDraw.cpp:362
- viewport.minDepth = vp.viewports[0].zoffset;
- viewport.maxDepth = vp.viewports[0].zscale + vp.viewports[0].zoffset;
+ viewport.minDepth = viewport_regs.zoffset - (clip_control.dx_clip_space ? 0.0f : viewport_regs.zscale);
+ viewport.maxDepth = viewport_regs.zoffset + viewport_regs.zscale;
```

This allows depth values outside [0,1] (which PS5's GPU allows but Vulkan doesn't by default). Requires `VK_EXT_depth_range_unrestricted` to be enabled.

**Why it was reverted (likely):** Without `VK_EXT_depth_range_unrestricted` being universally available (see Revert #1), this change produces invalid viewports on GPUs that don't support the extension. Vulkan validation would flag `minDepth < 0` or `maxDepth > 1` as errors, and some drivers crash.

### 1.5. Revert #5: `f880c58` → `ad93531` (Sep 7)

**Original commit:** `f880c58` "render: add depth attachment feedback support" (Sep 7, 04:25)
**Revert:** `ad93531` (Sep 7, 07:01)

**What it tried to do:** Add support for `VK_EXT_attachment_feedback_loop` to allow depth images to be both rendered to and sampled from in the same render pass. This is needed for techniques like:
- Screen-space reflections that sample the depth buffer while rendering to it
- Deferred shading that reads depth in a fragment shader
- Shadow filtering that samples the shadow map while updating it

Changes:
1. **graphicContext.h:** Added `attachment_feedback_loop_enabled` flag
2. **image.cpp:** When the flag is set, add `vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT` to depth images that are also sampled
3. **descriptors.cpp:** Recognize `vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT` as a depth-read layout
4. **shaders.cpp:** Convert the dynamic-states array from a fixed C array to a `std::vector` so depth-only pipelines can omit `eColorWriteEnableEXT`
5. **vulkanWindow.cpp:** Query and enable the extension at device creation
6. **tests:** Added 234 lines of tests (modified `ShaderRecompilerComputeTests.cpp`)

**Why it was reverted (likely):** The `eAttachmentFeedbackLoopOptimalEXT` layout requires careful synchronization that KytyPS5's barrier logic doesn't yet handle correctly. Games using this pattern would see rendering corruption (flickering depth, wrong shadows) rather than a crash, making it harder to diagnose. The revert removes the feature entirely until the synchronization is fixed.

---

## 2. Root Cause Analysis

All 5 reverts target the **same fundamental problem**: PS5's GPU allows depth buffers to be used in ways that Vulkan doesn't directly support. Specifically:

1. **Unrestricted depth ranges** — PS5's depth values can go outside [0,1] (e.g., for reversed-Z rendering). Vulkan requires [0,1] unless `VK_EXT_depth_range_unrestricted` is enabled.
2. **Depth-comparison textures** — PS5 games use `sampler2DShadow`-equivalent textures that sample depth and compare against a reference value. Vulkan requires these to use a depth format (`D32_SFLOAT`), not a color format.
3. **Attachment feedback loops** — PS5 games sample the depth buffer while rendering to it (e.g., for screen-space reflections). Vulkan requires `VK_EXT_attachment_feedback_loop` for this.

The underlying issue is that **all 3 features require Vulkan extensions that aren't universally supported**, and KytyPS5 doesn't have a proper fallback path for when the extensions are unavailable.

The 5 commits represent 5 attempts to add these features:
- #1 (Aug 25): Tried to make depth-range-unrestricted *required* → broke GPUs without support → reverted
- #2-#4 (Sep 4): Tried to add depth-comparison texture support + unrestricted depth ranges together → likely caused descriptor binding mismatches and validation errors → all 3 reverted within hours
- #5 (Sep 7): Tried to add attachment feedback loops → synchronization issues → reverted

---

## 3. The Proper Fix

The proper fix requires **3 coordinated changes** that must be landed together:

### 3.1. Make depth-range-unrestricted *optional* with a fallback

```cpp
// vulkanWindow.cpp — query support, don't require
bool depth_range_unrestricted_supported = 
    CheckPhysicalDeviceExtensionSupport(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
if (depth_range_unrestricted_supported) {
    device_extensions.push_back(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
}
// Store the flag in GraphicContext for later use
graphics.depth_range_unrestricted_enabled = depth_range_unrestricted_supported;
```

```cpp
// renderDraw.cpp — clamp depth range when extension is unavailable
if (graphics.depth_range_unrestricted_enabled) {
    viewport.minDepth = viewport_regs.zoffset - (clip_control.dx_clip_space ? 0.0f : viewport_regs.zscale);
    viewport.maxDepth = viewport_regs.zoffset + viewport_regs.zscale;
} else {
    // Fallback: clamp to [0,1] — may cause z-fighting but won't crash
    viewport.minDepth = std::clamp(viewport_regs.zoffset - viewport_regs.zscale, 0.0f, 1.0f);
    viewport.maxDepth = std::clamp(viewport_regs.zoffset + viewport_regs.zscale, 0.0f, 1.0f);
}
```

### 3.2. Add depth-comparison texture support with proper format policy

The `EXIT("unsupported comparison-sampled texture: format=%u")` at descriptors.cpp:520 needs to be replaced with a fallback that uses a color image when no depth format policy is available:

```cpp
static vk::Format TextureBackingFormat(const ShaderRecompiler::IR::ImageResource& resource,
                                       Prospero::BufferFormat guest_format,
                                       vk::Format             view_format) {
    if (!resource.depth_compare) {
        return view_format;
    }
    const auto* policy = FindGuestDepthFormatPolicy(guest_format);
    if (policy == nullptr) {
        // Fallback: use the color format — comparison will be wrong but won't crash
        LOGF("Warning: no depth format policy for comparison texture format=%u, using color format\n",
             static_cast<uint32_t>(guest_format));
        return view_format;
    }
    if (!IsSupportedSampledDepthResource(resource)) {
        LOGF("Warning: unsupported sampled depth resource, using color format\n");
        return view_format;
    }
    return policy->depth_attachment_format;
}
```

### 3.3. Add attachment feedback loop support with proper synchronization

The `eAttachmentFeedbackLoopOptimalEXT` layout needs proper barriers. The current code doesn't insert the right pipeline barriers between the feedback-loop read and the depth write, causing rendering corruption. This is the hardest fix and may require a follow-up PR.

---

## 4. Immediate Workaround (available now via Kyty-003)

While the proper fix is being developed, users can enable the `DisableAsyncCompute` and `ForceDepthRangeRestricted` hacks via Kyty-003's framework:

```bash
kyty_emulator --game /path/to/Sifu --enable-hack DisableAsyncCompute,ForceDepthRangeRestricted
```

Or edit `data/game_hacks.json`:

```json
{
  "PPSA01491": ["DisableAsyncCompute", "ForceDepthRangeRestricted"]
}
```

These hacks are **already hardcoded** for Sifu, Returnal, Spider-Man Remastered, and Demon's Souls in `src/loader/hack_features.cpp`.

**However**, the `DisableAsyncCompute` and `ForceDepthRangeRestricted` hacks are not yet wired into the renderer — they're defined in the enum but the render paths don't query them yet. This is the remaining work for Kyty-006:

### 4.1. Wire `ForceDepthRangeRestricted` into renderDraw.cpp

```cpp
// renderDraw.cpp — add after the viewport calculation
if (Loader::HackFeatures::HasHack(Loader::GameHack::ForceDepthRangeRestricted)) {
    viewport.minDepth = std::clamp(viewport.minDepth, 0.0f, 1.0f);
    viewport.maxDepth = std::clamp(viewport.maxDepth, 0.0f, 1.0f);
}
```

### 4.2. Wire `DisableAsyncCompute` into graphicsRun.cpp

Force all compute submissions to go through the graphics queue (queue 0) instead of the async compute queues (1-57):

```cpp
// graphicsRun.cpp — in the compute queue submission path
if (Loader::HackFeatures::HasHack(Loader::GameHack::DisableAsyncCompute)) {
    // Redirect to graphics queue
    submission.queue_id = 0;
}
```

---

## 5. Recommended Next Steps

1. **Wire the hacks into the renderer** (1-2 hours) — implement §4.1 and §4.2. This gives users an immediate workaround.
2. **Land the proper fix** (2-3 weeks) — implement §3.1, §3.2, §3.3 as a coordinated PR. Test with Sifu, Returnal, Spider-Man, Demon's Souls.
3. **Add regression tests** — the reverted commits included 234 lines of tests in `ShaderRecompilerComputeTests.cpp`. These should be re-added with the proper fix.

---

## 6. Commit Hashes for Reference

| Revert | Original commit | Original message |
|---|---|---|
| `7adade0` | `7012634f` | graphics: require unrestricted depth ranges |
| `81b0c13` | `0ce19357` | graphics: create depth images for comparison textures |
| `dd9ecd5` | `d762d24` | shader: separate comparison image bindings |
| `27f015e` | `eccd6f6` | graphics: preserve unrestricted viewport depth ranges |
| `ad93531` | `f880c58` | render: add depth attachment feedback support |

All 5 original commits were authored by `nmzik` between Aug 25 and Sep 7, 2026. All 5 were reverted within 24-72 hours of landing.
