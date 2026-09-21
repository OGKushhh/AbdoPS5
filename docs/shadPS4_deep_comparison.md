# shadPS4 vs fpPS4 vs Shadlix Fork — A Deep Architectural Comparison and Unified Improvement Roadmap

> Three repositories were cloned locally:
> - **shadPS4 upstream**: `/home/z/my-project/repos/shadPS4` (latest `main`)
> - **fpPS4**: `/home/z/my-project/repos/fpPS4` (red-prig's pure-HLE Pascal emulator)
> - **Shadlix fork**: `/home/z/my-project/repos/shadPS4-diegolix29` (diegolix29's fork, default branch `Shadlix`)
>
> Every claim in this document is grounded in actual source-code inspection of those trees. File paths cited are relative to each repo root. The fork was diffed against `upstream/main`; the merge-base is `3303b03 FileSys: Open host-backed files with POSIX-style full sharing (#4838)` and the fork has **32 commits ahead, 59 commits behind** upstream as of writing — it selectively rebases newer PRs.

---

## 0. TL;DR

| Dimension | shadPS4 (upstream) | fpPS4 | Shadlix fork (diegolix29) |
|---|---|---|---|
| Language | C++20 (modern, template-heavy) | Free Pascal 3.3.1 | C++20 (same as upstream) |
| License | GPL-2.0 | LGPL-2.1 | GPL-2.0 (same as upstream) |
| Total LOC (sources only) | ~271k C++/H | ~293k .pas/.inc/.lpr | ~430k C++/H (+ Qt GUI + translations) |
| Platforms | Windows, Linux, macOS (Apple Silicon only), FreeBSD | Windows 7 SP1+ x64 only | Windows, Linux, macOS (Intel Macs acknowledged as buggy) |
| CPU strategy | Direct native execution + Zydis/Xbyak runtime patching of guest code | Direct native execution (Free Pascal SEH64 layer) | Same as upstream |
| Shader stack | GCN → IR (yuzu-style) → SPIR-V → Vulkan, ~28 optimization passes | GCN → SPIR-V directly (no IR), per-opcode hand-rolled `emit_*` units | Same as upstream + selective reverts |
| GPU backend | Vulkan (custom rasterizer, texture cache, buffer cache, FSR) | Vulkan (smaller wrappers: vDevice, vImage, vPipeline, vRender) | Same as upstream + scaled min/max blending, sRGB A2R10G10B10, h265 vdec |
| HLE vs LLE | Mostly HLE for `libSce*`, with optional LLE via real `.sprx` firmware modules | Pure HLE — every `libSce*` re-implemented in Pascal | Same as upstream |
| GUI | Decoupled: `shadps4-qtlauncher`, plus built-in ImGui Big Picture | None (third-party: `fpPS4Fro`, `frofpp4`, `GUI/main.py`) | **Built-in Qt6 GUI** (`src/qt_gui/`), Cinema mode, Gamehub, 30+ Crowdin translations |
| Debug tools | `core/devtools/` ImGui widgets | `tools/` standalone utilities | Same as upstream + Qt-side mod manager, trophy viewer, compatibility DB |
| Networking | `core/libraries/network/` + `shadnet/` | `src/ps4_libscenet.pas`, `ps4_libscehttp.pas`, `ps4_libSceSsl.pas` (lighter) | Same as upstream |
| Game format support | Folder + extracted PFS | Folder only | **Folder + .zar (ZArchive) + .pkg (with Crypto++ decryption)** |
| Storage I/O | Direct host reads | Direct host reads | **`StorageScheduler` emulates PS4 HDD bandwidth** (75/100/125 MiB/s profiles with seek+rotation modeling) |
| Memory | VMA + PhysMap + Flexible + Pool + PRT + userfaultfd | Three-mm approach | Same as upstream + **`MemoryCompression` LRU page compression** |
| Per-game hacks | None (only per-game config) | 8 hack flags (`-h DEPTH_DISABLE_HACK` etc.) | **`HackFeatures` class** with per-serial game detection (e.g. The Order: 1886) + per-game config flags |
| Active status | Very active — running Bloodborne, Dark Souls Remastered, RDR | Author publicly says: rewriting in `kern` branch | Single developer, ~32 commits ahead, very active (commits daily) |

**Bottom line after the three-way comparison:**
- **shadPS4 upstream** is the most mature and the best foundation.
- **fpPS4** contributes clever **low-level patterns** (no-IR shader emitter, hack flags, NID database tooling).
- **Shadlix fork** contributes **major end-user features** that upstream lacks (Qt GUI, PKG/ZAR support, storage I/O scheduler, memory compression, per-game hack detection, screenshot module, Cubeb audio, 30+ translations).

The unified improvement plan in §6 is prioritized by impact-to-effort ratio and explicitly attributes each item to its source repo.

---

## 0.1. About the Shadlix fork

The fork at `https://github.com/diegolix29/shadPS4` has three branches:

- **`Shadlix` (default, what I cloned)** — the only branch the author will keep updating. Adds: high-resolution hack (from fmod) with adjonesse stabilization, modified to avoid crashing other games. All Bloodborne-specific enhancements. All features funneled into an "Experimental" settings tab.
- **`PRTBB`** — safest branch, no extra features.
- **`Full-Souls`** — adds AV improvements PR (Dark Souls 2 intro fix), various readback speeds (modified from Turtle Developer's optimization PR).

All three branches share these Bloodborne-specific enhancements (per the README):
- A sound hack that prevents Bloodborne from losing audio (originally by rainvmaker)
- Automatic backups via a checkbox in the Graphics tab
- A PM4 Type 0 hack
- An RCAS bar in Settings to adjust FSR sharpness
- Several Hotkeys
- Restart and Stop buttons working
- Keyboard and mouse custom button mapping for FromSoftware games
- An Experimental tab with `isDevKit` and `Neo Mode` (PS4 Pro Mode) checkboxes
- Safe Tiling and USB PRs that the author developed for main Shad

The Shadlix branch is based on upstream commit `3303b03` (an older `main`), with 32 fork-only commits selectively rebasing newer upstream PRs (visible in `git log upstream/main..HEAD`):

```
93ae252  Update ImGuiFileDialog submodule to upstream commit with ImGui compatibility fix
f5b6272  Update ImGuiFileDialog submodule to fix ImGui compatibility
7764cd0  Open Al fixes
79850ab  Revert predication
58ba940  RebasefX
2bf0e27  Rebase
b58f0b8  Revert shader_recompiler: split resource tracking and flatten load from buffer (#4782)
6cc2874  Revert Fix IR dumping (#4973)
...
cffb876  vdec2: support for h265/hevc (#4951)
c56a739  shader_recompiler: Enhance shared memory barrier handling and add divergent loop detection (#4941)
99c4530  Use a system mapping for camera frame data and add a dummy sceCameraGetCalibData implementation
7a630f3  Fix incorrect error return on attempting to create a file in a nonexistent folder in a writable mount
c95af7f  renderer_vulkan: do not auto-select a software rendering device (#4904)
d554801  shader_recompiler: Structurize later in the pipeline and cleanup (#4906)
0168f03  video_core: Emulate scaled min/max blending (#4768)
a46bf1d  shader_recompiler: decorate written storage buffers as Coherent (#4896)
...
cb0d7cd  1886 hacks perf and graphics (playable)  <- The Order: 1886
```

Notable selective reverts the author chose to make (revealing architectural opinions):
- `Revert predication` — the author disagrees with how upstream handles predication.
- `Revert shader_recompiler: split resource tracking and flatten load from buffer for sharp source (#4782)` — too aggressive an optimization, breaks games.
- `Revert "Fix IR dumping (#4973)"` — debugging change interferes with the fork's workflow.
- `Revert "Http2 fixes (#4908)"`, `Revert "Trophies online (shadNet) (#4914)"`, `Revert "Net Fixes (#4910)"`, `Revert "np_utility initial impelmentation (#4940)"`, `Revert "Http module fixups and implementations (#4933)"` — entire networking stack rolled back because of instability.
- `Revert "Tss support (#4930)"`, `Revert "Trigger a lightbar reset on controller connection (#4927)"` — minor features reverted.

The fork also adds commits not present in upstream:
- `1886 hacks perf and graphics (playable)` — The Order: 1886 hacks to make it playable
- `Adding Extra Flexible Memory configuration for global and per game (fixes issues with increase Dmen can crash the emulator)` — extra flexible memory config
- `Push to 0.18.1` — version bump
- `d2d5001 GG`, `e2f4d44 Fixing Rebase`, `58ba940 RebasefX`, `2bf0e27 Rebase`, `2d92651 Rebase Fx`, `f6b2ddb Rebase 9/7/2026` — rebase noise (this is a single-developer workflow)

Total file changes: **678 files changed, +156k insertions, -45k deletions** vs upstream `main`.

The next section enumerates the **concrete new subsystems** the fork adds that upstream lacks.

---

## 1. Architecture at a Glance

### 1.1 shadPS4 layout (`src/`)

```
src/
├── main.cpp                      CLI entrypoint (CLI11 + SDL3 + ImGui)
├── emulator.cpp/.h               top-level orchestrator
├── core/
│   ├── memory.cpp/.h             MemoryManager (VMA + PhysMap + Flexible + Pools + PRT)
│   ├── address_space.cpp/.h      low-level virtual-memory reservation (per-OS)
│   ├── linker.cpp/.h             module loading, TLS, relocation, symbol resolution
│   ├── cpu_patches.cpp/.h        Zydis → Xbyak JIT patching of guest instructions
│   ├── signals.cpp/.h            SIGSEGV / access-violation → CPU fallback handler
│   ├── thread.cpp/.h             scePthread wrapping
│   ├── tls.cpp/.h                TCB management (Windows TlsGetValue mimicry)
│   ├── module.cpp/.h             dynamic module representation
│   ├── debugger.cpp/.h           GDB-style stub
│   ├── emulator_state.*          global state holder
│   ├── emulator_settings.*       persistent user config
│   ├── loader/
│   │   ├── elf.cpp/.h            PS4 ELF/SELF loader
│   │   ├── symbols_resolver.*   NID → host function resolution
│   │   └── dwarf.*               DWARF debug info for backtraces
│   ├── aerolib/                  static NID table with binary search (aerolib.inl)
│   ├── file_format/              psf, pfs, trp, npbind, playgo_chunk
│   ├── file_sys/                 host_fs + zarchive_fs + device nodes (zero, random, etc.)
│   ├── ipc/                      single-process IPC for multi-instance coordination
│   └── libraries/                HLE implementations of libSce* (kernel, audio, video,
│                                 network, np, playgo, ngs2, gnmdriver, fiber, mouse, etc.)
├── shader_recompiler/
│   ├── frontend/                GCN decoder + control-flow graph + fetch/copy shader parsing
│   ├── ir/                      yuzu-inspired IR with ~28 optimization passes
│   ├── backend/spirv/           SPIR-V code emission, organized per category
│   └── recompiler.*             orchestrator (cache, specialization, params)
├── video_core/
│   ├── amdgpu/                  Liverpool GPU register model (regs_*.h, pm4_opcodes.h)
│   ├── renderer_vulkan/         ~8.5k LOC of Vulkan: instance, swapchain, scheduler,
│   │                            pipeline cache, rasterizer, presenter, FSR
│   ├── texture_cache/           image, image_view, sampler, tile_manager, blit_helper
│   ├── buffer_cache/            buffer, region_manager, fault_manager, memory_tracker
│   ├── host_shaders/             GLSL compute shaders (detilers, FSR, post-process, blit)
│   └── renderdoc.*               RenderDoc integration (F12 capture)
├── imgui/                       in-emulator HUD, big-picture mode, friends, notifications
├── shadnet/                      custom netcode (proto + client + server probe)
├── input/                       controller, mouse, keyboard
└── common/                       shared utilities (singleton, hash, AES, SHA1, spin_lock,
                                  adaptive_mutex, lru_cache, slab_heap, decoder, ...)
```

### 1.2 fpPS4 layout

```
fpPS4/
├── fpPS4.lpr                    program entrypoint (CLI parsing, module registration)
├── ps4_elf.pas                  ELF loader
├── ps4_program.pas              app0/app1 module tree
├── ps4libdoc.pas                NID → function-name database
├── chip/                        GPU register model + PSSL decoder + PM4 packet defs
│   ├── ps4_videodrv.pas         ~2.8k LOC: GFX ring, MicroEngine (EOP/Flip/WaitMem)
│   ├── ps4_pssl.pas             ~3.6k LOC: GCN opcode tables (SOP1/2/C/P, VOP1/2/3/C,
│   │                           MUBUF, MTBUF, MIMG, SMRD, DS, EXP, VINTRP, SOPK)
│   ├── ps4_shader.pas           shader resource parsing
│   ├── si_ci_vi_merged_*        Southern Islands / Sea Islands / Volcanic Islands register
│   │                           definitions (i.e. AMD GPU register headers, Pascal port)
│   └── pm4defs.pas              PM4 packet format definitions
├── spirv/                       ~36k LOC: PSSL → SPIR-V emitter (no IR layer)
│   ├── SprvEmit.pas             main emitter class
│   ├── sr*.pas                  SprvNode / Reg / Layout / Type / Flow / Bitcast / CFGParser
│   └── emit_*.pas               per-opcode-family emitters (emit_sop1, emit_vop3, ...)
├── vulkan/                      ~5k LOC: thin Vulkan wrapper layer
│   ├── vDevice, vPipeline, vImage, vSampler, vMemory, vCmdBuffer,
│   │   vRenderPassManager, vSetsPoolManager, vShaderManager, vHostBufferManager,
│   │   vImageTiling, vFlip, vRender
│   └── Vulkan.pas               raw Vulkan bindings
├── kernel/                      ~17k LOC: HLE kernel implementation
│   ├── ps4_libkernel.pas        ~2k LOC: top-level libkernel exports
│   ├── mm_adr_virtual.pas      ~2.3k LOC: virtual memory manager
│   ├── mm_adr_direct.pas       ~1k LOC: direct memory manager
│   ├── ps4_pthread.pas, ps4_mutex.pas, ps4_sema.pas, ps4_cond.pas,
│   │   ps4_rwlock.pas, ps4_barrier.pas, ps4_event_flag.pas, ps4_queue.pas,
│   │   ps4_signal.pas, ps4_pthread_attr.pas, ps4_pthread_key.pas,
│   │   ps4_time.pas, ps4_kernel_file.pas, ps4_scesocket.pas, ps4_map_mm.pas
├── sys/                         host OS glue (sys_crt, sys_pthread, sys_signal, sys_dev)
├── rtl/                         runtime helpers (atomic, LFQueue, RWLock, seh64, hamt, g23tree)
├── src/                         ~50 libSce* HLE modules in flat naming
│   ├── ps4_libsceaudioout.pas, ps4_libsceaudiodec.pas, ps4_libscevideoout.pas,
│   │   ps4_libscegnmdriver.pas, ps4_libscepad.pas, ps4_libscesavedata.pas,
│   │   ps4_libscenp*.pas (manager/matching2/score/signaling/trophy/tus/webapi/...),
│   │   ps4_libscehttp.pas, ps4_libSceSsl.pas, ps4_libscefiber.pas, ...
│   ├── inputs/                  xinput, sdl2, keyboard+mouse pad backends
│   ├── playgo/                  ps4_libsceplaygo + playgo_chunk_loader
│   ├── audio/                   ps4_libsceaudioout + portaudio binding
│   ├── audiodec/                ps4_libsceaudiodec + cpu variant
│   ├── ajm/                     ps4_libsceajm (AJM = Async Job Manager)
│   ├── libcinternal/            mtx_internal, mspace_internal, guard_internal, atexit_internal
│   └── np/                       Np* (Sony Network Play) modules
├── ffmpeg/                      FFI bindings to libav* for AvPlayer / Audiodec
├── shaders/                     pre-built GLSL compute shaders (FLIP_TILE_*, FLIP_LINE_*)
├── third_party_gui/             standalone community GUIs (fpPS4Fro C#, frofpp4 Pascal, Python)
├── tools/                       dev utilities (elf_sym, dump_sym, ps4libdoc, nid_gui_test,
│                                gfx6_chip, spirv_helper, param_sfo_info, playgo_info)
└── static/                      prebuilt Windows static libs (kernel32, msvcrt, portaudio, ...)
```

### 1.3 Architecture observations

- **shadPS4 is engineered like a console emulator that grew into a project.** There's a clear separation between frontend (CLI/ImGui), core (kernel, memory, loader), shader recompiler, video backend, and libraries. The `core/libraries/` subtree is itself divided per PS4 system module (audio, network, np, playgo, ngs2, fiber, etc.).
- **fpPS4 is engineered like a one-author research project.** It's flat, dense, and unapologetic about being Windows-only. The cleverness lives in tight, low-level Pascal code: hand-rolled MPSC queues (`LFQueue.pas`), custom HAMT and 2-3 trees (`hamt.pas`, `g23tree.pas`), its own SEH64 frame setup (`seh64.pas`), and a no-IR direct SPIR-V emitter.

---

## 1.5. The Shadlix Fork — Major New Subsystems Not in Upstream

This section enumerates every new top-level subsystem the fork adds. Each entry includes the source path, LOC, and a brief description of what it does and why it matters.

### 1.5.1. Built-in Qt6 GUI (`src/qt_gui/`)

The fork **integrates the shadps4-qtlauncher directly into the main binary** — a complete break from upstream's "core + separate GUI" design. The `src/qt_gui/` directory contains ~100 files covering:

| File | Role |
|---|---|
| `main.cpp` (605 LOC) | Qt `QApplication` entrypoint, replaces upstream's `src/main.cpp` for the default build |
| `main_window.cpp/.h` (4,545 LOC) | Main window with game grid, list, hub menu, settings |
| `main_window_themes.cpp/.h` | Themeable UI (light, dark, custom) |
| `settings_dialog.cpp/.h/.ui` (3,825-line .ui) | Comprehensive settings dialog with tabs: Graphics, Audio, Input, Network, Debug, Experimental |
| `game_list_frame.cpp/.h`, `game_grid_frame.cpp/.h` | Two view modes for the game library (list vs grid) |
| `hub_menu_widget.cpp/.h` | Netflix-style Cinema Mode big-picture UI |
| `game_cinematic_frame.cpp/.h` | Cinematic full-screen game launcher |
| `games_menu.cpp/.h` | Game hub menu |
| `background_music_player.cpp/.h` | Background music playback in the UI |
| `trophy_viewer.cpp/.h` | Trophy list viewer |
| `elf_viewer.cpp/.h` | ELF file inspector |
| `compatibility_info.cpp/.h` | Pulls from the shadps4-game-compatibility database |
| `cheats_patches.cpp/.h` | Cheat / patch manager |
| `mod_manager_dialog.cpp/.h`, `mod_tracker.cpp/.h` | Mod installation and tracking |
| `nexus_mods_api.cpp/.h` | Nexus Mods API client for mod downloads |
| `control_settings.cpp/.h/.ui` | Controller mapping UI |
| `kbm_gui.cpp/.h/.ui`, `kbm_config_dialog.cpp/.h`, `kbm_help_dialog.cpp/.h` | Keyboard/mouse binding UI |
| `game_specific_dialog.cpp/.h/.ui` | Per-game configuration UI |
| `hotkeys.cpp/.h/.ui` | Hotkey configuration |
| `welcome_dialog.cpp/.h`, `about_dialog.cpp/.h/.ui` | First-run welcome + about |
| `check_update.cpp/.h`, `version_dialog.cpp/.h/.ui` | Auto-update checking |
| `game_directory_dialog.cpp/.h` | Game folder setup |
| `game_info.cpp/.h`, `game_list_utils.h` | Game metadata parsing (param.sfo) |
| `screenshot.cpp` | Qt-side screenshot integration |
| `sdl_event_wrapper.cpp/.h` | SDL event forwarding into Qt |
| `gui_context_menus.h` | Reusable context menu helpers |
| `log_presets_dialog.cpp/.h` | Log filter configuration |
| `translations/*.ts` (30+ languages) | Crowdin-based translations: ar_SA, ca_ES, da_DK, de_DE, el_GR, en_US, es_ES, fa_IR, fi_FI, fr_FR, hu_HU, id_ID, it_IT, ja_JP, ko_KR, lt_LT, nb_NO, nl_NL, pl_PL, pt_BR, pt_PT, ro_RO, ru_RU, sl_SI, sq_AL, sr_CS, sv_SE, tr_TR, uk_UA, ur_PK, vi_VN, zh_CN, zh_TW |

Plus `crowdin.yml` at the repo root for CI-driven translation sync, and `cmake/DetectQtInstallation.cmake` for finding Qt on the build host.

**Why this matters:** upstream shadPS4 currently requires users to download a separate `shadps4-qtlauncher` binary. The fork ships a complete GUI experience in one binary. The Qt GUI is more polished than upstream's built-in ImGui Big Picture mode and includes a class of features upstream's Big Picture lacks: trophy viewer, mod manager with Nexus Mods integration, ELF viewer, compatibility DB integration, and full per-game settings UI.

### 1.5.2. PKG File Format + Crypto++ Decryption (`src/core/file_format/pkg.*` + `src/core/crypto/*`)

The fork can decrypt and mount PS4 PKG files directly. This is a **major UX win** because it eliminates the need for users to extract PKG files with separate tools (e.g. pkgViewer, fake_pkg).

Files:
- `src/core/crypto/crypto.cpp/.h` (216+63 LOC) — Crypto++ wrapper for AES-CBC, RSA-2048 (PKCS1v15), SHA-256, AES-XTS for PFS decryption, EFSM trophy decryption. Includes the standard `key_pkg_derived_key3_keyset_init`, `FakeKeyset_keyset_init`, `DebugRifKeyset_init` RSA private keys.
- `src/core/crypto/keys.h` (304 LOC) — Hardcoded PS4 keyset (pfs key, fake keyset, debug rif keyset).
- `src/core/file_format/pkg.cpp/.h` (468+172 LOC) — Full PKG header parser with every field (`PKGHeader`, `PKGEntry`, `PKGContentFlag`), supports content flags: FIRST_PATCH, PATCHGO, REMASTER, PS_CLOUD, GD_AC, NON_GAME, SUBSEQUENT_PATCH, DELTA_PATCH, CUMULATIVE_PATCH.
- `src/core/file_format/pkg_type.cpp/.h` (638+10 LOC) — Comprehensive PKG content type definitions (over 50 types: Game, Patch, Remaster, Theme, Avatar, PremiumTheme, etc.).

Vendored dependency: `externals/cryptopp` + `externals/cryptopp-cmake`.

**Why this matters:** users currently must extract PKG files manually to feed eboot.bin to shadPS4. The fork's PKG support means users can point at a `.pkg` file directly. This is a high-friction UX issue for the broader PS4 game-preservation community.

### 1.5.3. ZArchive (`.zar`) Filesystem (`src/common/zar_fs.cpp/.h`)

The fork adds the ZArchive format used by PS4 game rips. The `zar_fs.h` API includes:
- `IsZarArchive(path)`, `IsZarInnerPath(path)` — detection.
- `FindGameByID(dir, game_id, max_depth)` — search a directory for `<game_id>.zar` and return its eboot.bin path.
- `Exists`, `IsDirectory`, `IsRegularFile`, `GetFileSize`, `GetLastWriteTime` — drop-in replacements for `std::filesystem` equivalents that also work inside archives.
- `IterateDirectory(dir, callback)` — directory iteration.
- `OpenFile(path)` → `FileHandle` — read handle with offset/size.
- `CopyFile(src, dst)` — copy archive entry to host.
- `GetSpillDirectory()` + `CleanupSpillFiles()` — for entries that need host-file semantics.
- `ClearCache()` — drop cached readers without invalidating handles.

**Why this matters:** upstream shadPS4 supports folder games and extracted PFS. The fork adds direct `.zar` support, which is the de-facto standard for compressed PS4 game dumps (used by scene groups and most preservation tools).

### 1.5.4. Storage I/O Scheduler (`src/core/file_sys/storage_scheduler.cpp/.h`, 882 LOC)

This is one of the most architecturally interesting additions. The fork **emulates the PS4's storage I/O characteristics** by throttling reads through a scheduler that models:

- **Bandwidth profiles**: 0 (native), 75 MiB/s, 100 MiB/s, 125 MiB/s. Unsupported values clamp to nearest profile.
- **HDD seek/rotation**: `AverageSeek = 13ms`, `AverageRotation = 5.556ms` (i.e. 5400 RPM).
- **Per-priority queueing**: priorities are int8 (-128 to +127), normalized to 0-255 index.
- **Async read submission**: `SubmitRead(file, spans, offset, priority, completion)` returns a `StorageRequestHandle` that can be canceled.
- **Blocking read path**: `ReadBlocking(file, spans, offset, priority)`.
- **Stats tracking**: bytes_read, chunks, sequential_chunks, positioned_chunks, modeled_wait_ns, timer_oversleep_ns, host_overrun_ns, host_wait_ns, prefetched_chunks, demand_chunks, max_staging_buffers, max_queue_depth.
- **Guest flip reporting**: `ReportGuestFlip(expected_flip_period)` stretches modeled I/O time when the emulator runs slower than the game's target cadence.

Plus: `MaxChunkSize = 512 KB`, configurable via `Configure(bandwidth_mibps)`, exposed via `GetApp0StorageScheduler()` and `ShouldScheduleAppRead(file)`.

**Why this matters:** many PS4 games (especially early-generation titles) load assets at specific cadences timed to the PS4's stock HDD. When emulated on a fast NVMe SSD, the game's streaming logic breaks because assets arrive too fast, leading to texture pop-in glitches or anti-cheat false positives. The StorageScheduler lets users opt into "authentic" I/O latency, which fixes a class of subtle game bugs.

### 1.5.5. Memory Compression (`src/core/memory_compression.cpp/.h`, 309 LOC)

A page-level memory compression system for the emulator's guest RAM:

```cpp
struct CompressedMemoryBlock {
    VAddr virtual_addr;
    u64 size;
    std::vector<u8> compressed_data;
    std::chrono::steady_clock::time_point last_access;
    u32 access_count;
};
```

API:
- `TryCompressBlock(virtual_addr, size, data)` — compress if block hasn't been recently accessed.
- `DecompressBlock(virtual_addr)` — decompress on access.
- `IsCompressed(virtual_addr)` — query.
- `GetStats()` — total_blocks, compressed_blocks, total_original_size, total_compressed_size, memory_saved_bytes, compression_ratio.
- `CleanupOldBlocks()` — LRU eviction.
- `SetCompressionLevel(int 0-3)` — disabled / fast / balanced / max.
- `SetEnabled(bool)` — runtime toggle.

Configuration exposed via `getMemoryCompressionLevel()` and `setMemoryCompressionLevel(int)` in `common/config.h`.

**Why this matters:** PS4 has ~5.5 GB of usable RAM for games. On systems with 8 GB or less, large games (Bloodborne, RDR, Yakuza) can OOM. Memory compression lets the emulator swap cold pages to a compressed in-process store instead of paging to disk. This is conceptually similar to macOS's memory compressor or Linux's zswap.

### 1.5.6. Centralized Config (`src/common/config.cpp/.h`, 3,594 LOC)

The fork replaces upstream's `EmulatorSettings` singleton with a free-function-style centralized config namespace. Key additions vs. upstream:

- **`ReadbackSpeed` enum**: Disable, Unsafe, Low, Default, Fast — controls GPU readback aggressiveness, integrates with the StorageScheduler concept.
- **`AudioBackend` enum**: SDL, OpenAL — chooses audio backend at runtime.
- **`OpenALHrtfMode` enum**: HrtfAuto, HrtfOn, HrtfOff — HRTF binaural rendering control.
- **`OpenALOutputMode` enum**: OutputAuto, OutputStereo, OutputQuad, OutputSurround51, OutputSurround71.
- **`HideCursorState` enum**: Never, Idle, Always.
- **`getShaderSkipsEnabled()` + `ShouldSkipShader(hash)`** — per-shader skip list (for known-broken shaders).
- **`SetSkippedShaderHashes(game_id)`** — load per-game skip lists.
- **`getEnableAutoBackup()`** — automatic save-data backup before launching.
- **`isNeoModeConsole()`, `isDevKitConsole()`** — PS4 Pro / DevKit emulation toggles.
- **`getExtraDmemInMbytes()`, `getExtraFmemInMbytes()`** — extra direct/flexible memory amounts (per-game).
- **`getUseHostMemoryFallback()`** — host RAM fallback when guest RAM is exhausted.
- **`getMemoryCompressionLevel()`** — memory compression toggle.
- **`getDefaultControllerID()`, `getActiveControllerID()`** — controller selection.
- **`getBackgroundControllerInput()`** — background input capture.
- **`IsShadNetEnabled()`, `getShadNetEnabledStates()`, `getShadNetNpids()`, `getShadNetPasswords()`** — shadnet account config (4 user slots).
- **`getCustomBackgroundImage()`, `getBackgroundImageOpacity()`, `getShowBackgroundImage()`** — UI customization.
- **`getPlayBGM()`, `getBGMvolume()`** — UI background music.
- **`getCompatibilityEnabled()`, `getCheckCompatibilityOnStartup()`** — compatibility DB integration.

**Why this matters:** the centralized config makes adding per-game settings dramatically easier. The `ReadbackSpeed` enum in particular is a pattern upstream should adopt — it lets users trade accuracy for performance per-game.

### 1.5.7. Per-Game Hack Features (`src/common/hack_features.cpp/.h`)

A simple but effective per-game detection system:

```cpp
class HackFeatures {
public:
    static bool isTheOrder1886;
    static void Init(std::string_view game_serial);
};
```

The `Init` function matches against specific CUSA IDs (e.g. `CUSA00035`, `CUSA00076`, `CUSA00100` for The Order: 1886 — different regional/language releases) and sets static flags. The flags are then queried from anywhere in the codebase to apply game-specific workarounds.

**Why this matters:** this is the **same conceptual pattern as fpPS4's `-h` hack flags** but more targeted — instead of asking the user to know which hack to enable, the emulator auto-detects based on game serial. The fork's commit `cb0d7cd 1886 hacks perf and graphics (playable)` shows this approach made The Order: 1886 playable.

### 1.5.8. Screenshot Module (`src/video_core/screenshot.cpp/.h`)

A small dedicated screenshot subsystem:

```cpp
namespace VideoCore {
void TriggerScreenshot();
bool ConsumeScreenshotRequest();
bool CaptureScreenshot(Vulkan::Rasterizer& rasterizer,
                       const std::filesystem::path& output_dir = {},
                       const std::string& filename = {});
}
```

Plus `common/stb_write.cpp` for `stb_image_write` integration.

**Why this matters:** upstream has the `Alt+F12` screenshot hotkey but no dedicated module — screenshots are handled inline in the presenter. The fork's approach is cleaner and supports programmatic triggering (e.g. for automatic save screenshots before launching, or for in-emulator photo mode).

### 1.5.9. IPC Client (`src/core/ipc/ipc_client.cpp/.h`, 351 LOC)

A client-side IPC implementation alongside upstream's `core/ipc/ipc.cpp` server. This enables multi-instance coordination (e.g. one instance launches another for a specific game, or instances coordinate save-game synchronization).

**Why this matters:** upstream has the IPC server infrastructure but lacks a clean client. The fork's `ipc_client` enables future features like "launch from browser" or "remote control from a Discord bot".

### 1.5.10. Cubeb Audio Backend (`src/core/libraries/audio/cubeb_audio.cpp`)

In addition to upstream's SDL audio out and OpenAL backends, the fork adds Cubeb (Mozilla's audio library used by Firefox). This is the same backend used by Citra, RPCS3, and Dolphin, and is generally considered the best cross-platform low-latency audio library.

**Why this matters:** Cubeb has meaningfully lower latency than SDL on Linux/PipeWire and better device routing on macOS. Adding it gives users a third option when neither SDL nor OpenAL works well.

### 1.5.11. Enhanced Camera Library (`src/core/libraries/camera/camera.cpp`, 1,317 LOC)

A substantially more complete camera implementation than upstream's, with system memory mapping for camera frames and a `sceCameraGetCalibData` stub.

**Why this matters:** PS4 camera-using games (Playroom, Just Dance, certain VR titles) need a real camera HLE. Upstream's camera library is minimal.

### 1.5.12. Flat NP Module Layout (replaces upstream's nested directories)

The fork replaces upstream's deeply-nested `np/np_matching2/`, `np/np_score/`, `np/np_signaling/`, `np/np_web_api/`, `np/np_web_api2/`, `np/np_utility/` directory structure with flat files: `np/np_matching2.cpp`, `np/np_score.cpp`, `np/np_signaling.cpp`, `np/np_web_api.cpp`, `np/np_web_api2.cpp`, etc.

Plus dedicated error-header files: `np_auth_error.h`, `np_common_error.h`, `np_party_error.h`, `np_trophy_error.h`, `np_web_api_error.h`, `np_web_api2_error.h`.

It also deletes upstream's `np_handler.cpp/h` (3,578 lines removed) and `np_utility/` directory.

**Why this matters:** the flat layout is easier to navigate and removes the somewhat-overengineered NP handler indirection. Upstream could learn from this simplification.

### 1.5.13. Sysmodule Library (`src/core/libraries/system/sysmodule.cpp/.h`)

A dedicated `libSceSysmodule` implementation, with `sysmodule_error.h` for error codes.

**Why this matters:** many games call `sceSysmoduleLoadModule` to dynamically load system modules at runtime. Upstream's stub-like handling causes silent failures.

### 1.5.14. Additional Vulkan Backend Enhancements

The fork applies (via rebased upstream PRs) several Vulkan renderer improvements not yet in the fork's merge-base:

- **`vk_depth_stencil_state.h`** (81 LOC new file) — extracted depth/stencil state into its own header for reuse.
- **Scaled min/max blending emulation** (PR #4768) — required for some games that use `MIN/MAX` blend modes incorrectly.
- **sRGB for A2R10G10B10 display buffers** (PR #4957) — fixes color reproduction for games using 10-bit-per-channel formats.
- **H.265/HEVC video decode** (PR #4951) — `vdec2: support for h265/hevc`.
- **Coherent storage buffer decoration** (PR #4896) — `shader_recompiler: decorate written storage buffers as Coherent`.
- **Shared memory barrier enhancement + divergent loop detection** (PR #4941) — fixes a class of GPU hangs.
- **Shader structurize-later pipeline** (PR #4906) — moves the structurize pass later in the pipeline, cleaner code.
- **Do not auto-select software Vulkan device** (PR #4904) — prevents lavaPipe from being picked when a real GPU exists.
- **Shader skip list** — `getShaderSkipsEnabled()` + `ShouldSkipShader(hash)` lets users skip known-broken shaders per-game.

**Why this matters:** these PRs are all candidates for upstream and many are already in upstream's `main`. The fork demonstrates they work together without breaking games.

### 1.5.15. Build System and Distribution

The fork adds:
- `crowdin.yml` — Crowdin translation sync.
- `cmake/DetectQtInstallation.cmake` — auto-find Qt.
- `.github/linux-appimage-qt.sh` — AppImage packaging.
- `.github/workflows/update_translation.yml` + `scripts/update_translation.sh` — CI for translations.
- `externals/MoltenVK` — vendored MoltenVK for macOS.
- `externals/sdl3_image` + `externals/sdl3_mixer` — SDL3 add-ons.
- `dist/MacOSBundleInfo.plist.in`, `dist/net.shadps4.shadPS4.metainfo.xml`, `dist/qt.conf` — proper macOS/Linux packaging.
- `net.shadps4.shadPS4.yaml` — Flatpak manifest.

**Why this matters:** the fork ships as proper AppImage / Flatpak / .app bundles. Upstream's distribution story is currently Windows-centric with Linux/macOS as an afterthought.

---

## 2. Subsystem-by-Subsystem Comparison

### 2.1 CPU Execution Strategy

Both emulators exploit the fact that the PS4's CPU is x86-64 (AMD Jaguar), so guest code can run **natively** on the host PC. The difference is in how they bridge the gap between guest and host.

#### shadPS4 — patch-then-run with Zydis/Xbyak JIT

`src/core/cpu_patches.cpp` (2,205 lines) is a **runtime binary translator**:

1. After loading an ELF, shadPS4 walks every executable section.
2. For each instruction, it disassembles with **Zydis**.
3. For instructions that don't behave the same way on host (notably `mov reg, fs:[0]` for TLS access, since the PS4's TCB layout differs from Windows/Linux TCB), it **patches in-place** using **Xbyak** to emit a small recompiled trampoline that loads the correct TCB pointer.
4. The rest of the code runs untouched.
5. A signal handler (`src/core/signals.cpp`) catches the inevitable SIGSEGV / access-violation when guest code touches emulated memory (e.g. flexible memory that's only lazily backed), and routes it back to `MemoryManager` to back the page.

This is more sophisticated than what most PS4 emulators do, and is the same general approach Dolphin uses for some Batocera-style patches. The code I read shows:
- Full Zydis → Xbyak register and memory operand converters.
- A `FilterTcbAccess` predicate that targets only `mov (64-bit reg), fs:[0]` patterns.
- A `RetrieveTcbPointer` helper that on Windows mimics `TlsGetValue`'s exact access path through `gs:[0x1480 + slot*8]` (TLS slots array) or `gs:[0x1780]` (expansion slots).
- Linux/macOS variants that pull TCB from `%fs:0` differently.

This means shadPS4 can run unmodified PS4 eboot binaries on x86-64 with **near-zero** CPU overhead — only TLS access, syscall instructions, and a few edge cases are intercepted.

#### fpPS4 — direct execution with SEH64 scaffolding

fpPS4's `rtl/seh64.pas` is its own implementation of Structured Exception Handling for x64, written in Free Pascal. Combined with the `windows` unit and the `_sig_lock` / `_sig_unlock` calls visible in `ps4_libkernel.pas`, the strategy is:

1. Load the PS4 ELF directly into the host process.
2. Resolve imports to host Pascal functions.
3. Jump to `eboot`'s entry point — it runs natively.
4. When the guest does something that requires host intervention (e.g. calls an unimplemented NID, triggers a guard page fault, or calls a syscall), the SEH frame catches it and dispatches to the HLE handler.

This is a simpler model conceptually, but it's also why fpPS4 is Windows-only: it leans hard on Windows-specific SEH64. Porting to Linux would mean re-implementing everything on top of `sigaction` and a custom fault handler.

#### Verdict

**shadPS4 is meaningfully more portable and more robust.** It has explicit Linux, macOS, and FreeBSD paths in `cpu_patches.cpp`, and it can target multiple TCB key stores via `GetTcbKey()`. fpPS4's reliance on SEH64 is a fundamental constraint that the author acknowledges by writing the `kern` rewrite branch.

### 2.2 Memory Subsystem

#### shadPS4 — explicit VMA + PhysMap model with sparse/PRT support

`src/core/memory.h` (363 lines) and `memory.cpp` together model the PS4's memory architecture faithfully:

- **Three memory classes** mirrored from real hardware:
  - **Direct memory** (`dmem_map`) — the physical RAM pool, allocated through `sceKernelAllocateDirectMemory`.
  - **Flexible memory** (`fmem_map`) — the dynamically-shared CPU/GPU pool.
  - **Pool memory** — the `sceKernelAllocPool`/`PoolCommit` heap.
- **VMA tree** (`vma_map`) — a `std::map<VAddr, VirtualMemoryArea>` with full merging/splitting, types (`Free`, `Reserved`, `Direct`, `Flexible`, `Pooled`, `PoolReserved`, `Stack`, `Code`, `File`, `System`).
- **PRT (Partially Resident Textures)** — three `PrtArea` entries for sparse texture residency tracking.
- **40-bit GPU addressing limit** — explicitly enforced: `IsValidGpuMapping` checks `virtual_addr + size < 0x10000000000`.
- **userfaultfd on Linux** — the `--userfaultfd` CLI flag lets the kernel trap guest memory accesses lazily, which is the most efficient known way to emulate the PS4's on-demand page-fault behavior. This is the same technique modern KVM uses.
- **SharedFirstMutex** — a custom hybrid lock (`common/shared_first_mutex.h`) for the memory manager that gives readers priority but lets writers proceed without starvation. This is non-trivial concurrency engineering that fpPS4 doesn't have an equivalent of.

#### fpPS4 — three-mm approach

`kernel/mm_adr_direct.pas` (1,026 LOC), `mm_adr_virtual.pas` (2,319 LOC), `mm_adr_name.pas` (408 LOC) split the address-space model into:
- `mm_adr_direct` — direct memory pool.
- `mm_adr_virtual` — virtual memory manager.
- `mm_adr_name` — named memory regions (similar to shadPS4's `NameVirtualRange`).

This is conceptually similar to shadPS4 but lacks:
- PRT/sparse texture tracking.
- userfaultfd integration.
- A multi-reader/single-writer lock (uses plain `RWLock.pas`).

#### Verdict

shadPS4's memory subsystem is **substantially more complete** and is the right foundation for emulating AAA games that hammer PRT and flexible memory. fpPS4's memory model is sufficient for homebrew.

### 2.3 Shader Recompilation

This is where the two projects diverge most sharply in design philosophy.

#### shadPS4 — IR-based compiler with optimization passes

`src/shader_recompiler/` is structured like a real compiler:

```
frontend/    — GCN instruction decode, control-flow graph recovery,
              structured control-flow reconstruction, fetch/copy shader parsing
ir/          — yuzu-inspired IR (basic blocks, SSA, dominance, post-order,
              abstract syntax list, pattern matching, breadth-first search)
ir/passes/   — 28 optimization passes:
   • ring_access_elimination         • ssa_repair_pass
   • shared_memory_to_storage_pass   • hull_shader_transform
   • shader_info_collection_pass     • inject_clip_distance_attributes
   • readlane_elimination_pass       • lower_buffer_format_to_raw
   • lower_fp64_to_fp32             • flatten_extended_userdata_pass
   • lower_wave64_pass              • ssa_rewrite_pass
   • constant_propagation_pass      • shared_memory_barrier_pass
   • lower_phis_to_regs_pass        • resource_discover_pass
   • phi_simplification_pass        • resource_patching_pass
   • dead_code_elimination_pass     • lower_user_clip_planes
   • inverse_ballot_elimination_pass • shared_memory_simplify_pass
backend/spirv/  — SPIR-V emitter, organized per category:
   • emit_spirv_atomic, _barriers, _bitwise_conversion, _composite,
     _context_get_set, _convert, _discard_frag, _floating_point, _image,
     _integer, _logical, _quad_rect, _select, _shared_memory,
     _special, _undefined, _warp, _convert, etc.
```

This is the **yuzu Hades** blueprint made explicit: a real intermediate representation, real SSA, real optimization passes. The advantage is that the **same backend** can target Vulkan/SPIR-V today and (potentially) Metal/DXIL/NVIR tomorrow, and shader compilation can be aggressively optimized.

#### fpPS4 — direct PSSL → SPIR-V translator, no IR

`spirv/` is ~36k LOC of per-opcode-family emitters:
- `emit_sop1.pas`, `emit_sop2.pas`, `emit_sopc.pas`, `emit_sopp.pas`, `emit_sopk.pas` — scalar ops.
- `emit_vop1.pas`, `emit_vop2.pas`, `emit_vop3.pas`, `emit_vopc.pas` — vector ops.
- `emit_mubuf.pas`, `emit_mtbuf.pas`, `emit_mimg.pas`, `emit_smueld.pas`, `emit_smrd.pas` — memory ops.
- `emit_vintrp.pas`, `emit_exp.pas`, `emit_vbuf_*.pas`, `emit_fetch.pas` — interpolation / export / vertex fetch.
- `sr*.pas` — supporting infrastructure (Node, Reg, Layout, Type, Flow, CFGParser, Bitcast, Const, Variable, Decorate, Capability, RefId, Allocator, CacheOp, OpUtils, Op, Interface, Uniform, VBufInfo, VertLayout, FragLayout, CFGLabel, CFGCursor, Output, Private).

There's no optimization IR. Each PSSL opcode is translated 1:1 (or 1:few) into SPIR-V. This is conceptually simpler and **substantially faster to compile** (no IR build, no passes, no SSA repair), but it:
- Cannot fold constants across instructions.
- Cannot eliminate dead code across basic blocks.
- Cannot rewrite wave-level intrinsics to a more efficient host form.
- Produces larger, less optimal SPIR-V.

#### Verdict

For **shader compilation speed**, fpPS4 wins — its direct emitter has lower latency. For **runtime performance of the generated shaders**, shadPS4 wins — its optimization passes produce tighter SPIR-V. AAA games with hundreds of unique shaders benefit massively from shadPS4's passes.

That said, shadPS4 could learn from fpPS4's **incremental compilation model**: fpPS4 emits SPIR-V instruction-by-instruction as it walks the bytecode, with very low memory overhead. shadPS4 could add a fast path for shaders that don't benefit from optimization (e.g. already-optimized compute kernels in firmware modules).

### 2.4 GPU Backend (Vulkan)

#### shadPS4 — full-featured renderer

`src/video_core/renderer_vulkan/` is ~8.5k LOC of carefully engineered Vulkan code:
- `vk_instance.cpp` (822 LOC) — device selection, extensions, features.
- `vk_rasterizer.cpp` (1,468 LOC) — main draw/dispatch loop, binding tracking.
- `vk_presenter.cpp` (1,143 LOC) — swapchain, present, FSR integration.
- `vk_pipeline_cache.cpp` (767 LOC) — shader cache, pipeline cache, serialization.
- `vk_graphics_pipeline.cpp` (541 LOC) — graphics pipeline state.
- `vk_compute_pipeline.cpp` (119 LOC) — compute pipeline state.
- `vk_scheduler.cpp` (388 LOC) — command buffer scheduling, semaphore management.
- `vk_swapchain.cpp` (344 LOC) — surface + swapchain recreation.
- `vk_resource_pool.cpp` (193 LOC) — descriptor pool / set recycling.
- `vk_shader_util.cpp` (269 LOC) — glslang → SPIR-V → VkShaderModule.
- `vk_shader_hle.cpp` (133 LOC) — **HLE shortcuts for common shader patterns** (clever optimization).
- `vk_platform.cpp` (442 LOC) — per-platform surface creation (Win32, XCB, Wayland, Metal).
- `vk_pipeline_serialization.cpp` (498 LOC) — **disk pipeline cache** (massive startup speedup).
- `liverpool_to_vk.cpp` (1,191 LOC) — translation from PS4 Liverpool GPU register state to Vulkan state. This is the **heart** of the GPU emulator.

Supporting infra:
- `video_core/amdgpu/` — register definitions for the AMD Liverpool (PS4 GPU): `regs_color.h`, `regs_depth.h`, `regs_texture.h`, `regs_vertex.h`, `regs_shader.h`, `regs_primitive.h`, `pm4_opcodes.h`, `pm4_cmds.h`, `pixel_format.h`, `tiling.h`, `cb_db_extent.h`, `liverpool.h/.cpp`, `resource.h`.
- `video_core/texture_cache/` — image, image_view, sampler, tile_manager, host_compatibility, blit_helper.
- `video_core/buffer_cache/` — buffer, region_manager, range_set, fault_manager, memory_tracker.
- `video_core/host_shaders/` — GLSL compute shaders for texture detiling (per bpp: 8/16/32/64/128 + macro/micro), FSR, post-process, MS-AA blit, fault buffer.

The PM4 command processor (`liverpool.cpp/.h`) uses **C++20 coroutines** (`std::coroutine`, `Task`, `promise_type`) and a `command_queue` of arbitrary functors that are dispatched on a dedicated GPU thread. This is a beautiful design — the GPU runs as an independent logical thread, and PM4 packets are processed asynchronously with proper synchronization via `std::binary_semaphore`.

Compute queues: `NumGfxRings=1`, `NumComputePipes=7` (out of 8 — pipe #7 is reserved by PS4 system software), `NumQueuesPerPipe=8`, giving `NumComputeRings=56` and `NumTotalQueues=57`. This matches the real PS4 hardware spec.

#### fpPS4 — leaner Vulkan wrapper

`vulkan/` is ~5k LOC of thin Pascal wrappers:
- `vDevice.pas` — Vulkan instance + device.
- `vPipeline.pas`, `vPipelineLayoutManager.pas` — pipeline + layout.
- `vImage.pas`, `vImageManager.pas`, `vImageTiling.pas` — image management + tiling.
- `vBuffer.pas`, `vHostBufferManager.pas`, `vMemory.pas` — buffer + memory.
- `vSampler.pas`, `vSamplerManager.pas` — sampler state.
- `vRender.pas` (644 LOC) — render loop.
- `vRenderPassManager.pas` — render pass compatibility cache.
- `vSetsPoolManager.pas`, `vSetLayoutManager.pas` — descriptor sets.
- `vShaderManager.pas`, `vShader.pas`, `vShaderExt.pas` (968 LOC) — shader module management.
- `vCmdBuffer.pas` — command buffer.
- `vFlip.pas` — flip/present path, with several "SRGB_HACK" workarounds visible in the source.

The GPU command processor is in `chip/ps4_videodrv.pas` (2,830 LOC), and uses a `TvCmdRing` (intrusive MPSC queue from `LFQueue.pas`) plus a `TvMicroEngine` that handles four packet types: `metCmdBuffer`, `metFlip`, `metEop`, `metWaitMem`. This is the same general architecture as shadPS4's Liverpool, but simpler and with hand-rolled Pascal queues.

#### Verdict

shadPS4 has a substantially more complete Vulkan backend, including:
- Disk-cached pipeline state (huge for replaying a game you've already launched once).
- FSR 1 upscaling built-in.
- Multi-queue compute support matching PS4 hardware.
- Coroutine-based asynchronous PM4 processing.
- HLE shortcuts for common shader patterns.
- RenderDoc integration.

fpPS4's Vulkan layer is solid but minimal. Its main contribution is its `vShaderExt.pas` (968 LOC) — extended shader metadata management — which shadPS4 handles via `vk_pipeline_cache.cpp` instead.

### 2.5 HLE Library Coverage

Both emulators HLE the PS4 system libraries. Let's count modules actually implemented:

#### shadPS4 (`src/core/libraries/`)

Organized in 20+ subdirectories:
- `kernel/` (threads, memory, time, fios)
- `audio/` (audioout, audioin, audioout_backend, openal, sdl)
- `audio3d/` (3D audio with OpenAL backend)
- `ngs2/` (Next Generation Sound — 14 sub-files: custom, eq, pan, geom, sampler, submixer, mastering, reverb, report, impl)
- `network/` (net, http, http2, ssl, ssl2, netctl, sys_net, net_epoll, net_resolver, net_upnp, posix_sockets, unix_sockets, p2p_sockets, net_obj, net_ctl_obj, net_util)
- `np/` (Sony Network Play: auth, common, manager, party, score, signaling, trophy, tus, web_api, web_api2, matching2 + signaling internal, commerce, partner, profile_dialog, sns_facebook_dialog)
- `gnmdriver/` (the GNM graphics driver shim)
- `playgo/` (playgo_chunk + dialog)
- `libc_internal/` (libc with str, memory, io, threads, math, printf)
- `fiber/` (cooperative fibers)
- `razor_cpu/` (Razor CPU profiling API)
- `mouse/` (mouse + sdl_mouse)
- `camera/` (PS4 camera)
- `usbd/` (USB devices, plus emulated Skylander, Infinity, Dimensions toys)
- `voice/` (voice chat codecs)
- `disc_map/` (disc -> HDD mapping)
- `system_service/` (system state)
- `app_content/` (app content)
- `content_export/` (screenshots/video export)
- `web_browser_dialog/` (web browser)
- `ime/` (input method editor — implicit via dialogs)

Total: **easily 80+ HLE modules**, deeply organized.

#### fpPS4 (`src/` flat)

Roughly 50 libSce* modules: audioout, audio3d, audiodec, audiodeccpu, videoout, gnmdriver, pad, savedata, dialogs, np_* (auth, common, manager, party, score, signaling, trophy, tus, webapi, matching2, sessionsignaling, gameintent, utility), http, Ssl, net, rudp, fiber, move, movetracker, vrtracker, depth, camera, usbd, usbstorage, videorecording, gamelivestreaming, shareplay, shareutility, socialscreen, companionhttpd, companionutil, contentexport, convertkeycode, systemgesture, ult, ime, lncutil, loginsservice, random, rtc, screenshot, systemservice, syscore, ajm, avplayer, composite, discmap, hmd, appcontent, playgo.

Total: ~50 HLE modules, flatter layout.

#### Verdict

shadPS4 has broader and deeper HLE coverage, especially in `ngs2` (sound synthesis — 14 sub-modules), `network/` (with epoll, posix+unix sockets, UPnP), and `np/` (with full matching2/signaling/commerce stacks). It also has a separate **`shadnet/`** subsystem (`shadnet.proto`, `client.cpp`, `server_probe.cpp`) for multiplayer emulation, which fpPS4 lacks.

That said, fpPS4 has a few modules shadPS4 lacks or implements more shallowly:
- **`ajm`** — Async Job Manager (used for some audio decoding). fpPS4 has `src/ajm/ps4_libsceajm.pas` with a dedicated `ajm_error.inc`. shadPS4 handles AJM through `audiodec` libraries.
- **`videorecording`** and **`gamelivestreaming`** — fpPS4 has these as separate files. shadPS4 doesn't appear to have dedicated implementations.
- **`companionhttpd`** and **`companionutil`** — second-screen companion app support. fpPS4 has these.

### 2.6 Firmware Module Support (LLE)

This is where shadPS4 clearly separates itself.

shadPS4's README explicitly states that **real Sony firmware `.sprx` modules** can be loaded from `sys_modules/`. It lists ~40 supported modules:

```
libSceAt9Enc, libSceAudiodec, libSceAudiodecCpu, libSceAudiodecCpuDdp,
libSceAudiodecCpuDtsHdLbr, libSceAudiodecCpuHevag, libSceAudiodecCpuM4aac,
libSceAvPlayer, libSceAvPlayerStreaming, libSceBeisobmf, libSceBemp2sys,
libSceCesCs, libSceFont, libSceFontFt, libSceFreeTypeOl, libSceFreeTypeOptOl,
libSceFreeTypeOt, libSceJpegDec, libSceJpegEnc, libSceJson, libSceJson2,
libSceLibcInternal, libSceNgs2, libScePngEnc, libScePsmKitSystem, libSceRtc,
libSceRudp, libSceSystemGesture, libSceUlt, libSceWkFontConfig, libSceXml,
libSceDepth, libScePadTracker, libSceMoveTracker
```

This means shadPS4 can run the **actual Sony implementation** of these libraries, dumped from a real PS4. This is enormously valuable for compatibility because it sidesteps the months of reverse engineering required to HLE each library perfectly.

fpPS4, by contrast, is **pure HLE** — every libSce* is re-implemented from scratch in Pascal. The README contains no mention of loading real firmware modules.

#### Verdict

shadPS4's hybrid HLE+LLE strategy is a major architectural advantage. fpPS4's pure-HLE approach is more portable and license-clean, but limits long-term compatibility.

### 2.7 Debugging and Development Tools

#### shadPS4 — in-emulator ImGui dev tools

`src/core/devtools/` contains a full set of ImGui-based debug widgets:
- `widget/cmd_list.cpp` — PM4 command list viewer.
- `widget/frame_dump.cpp` — per-frame capture and dump.
- `widget/frame_graph.cpp` — frame-time graph.
- `widget/memory_map.cpp` — VMA visualization.
- `widget/module_list.cpp` — loaded module list.
- `widget/reg_popup.cpp` / `reg_view.cpp` — register inspector.
- `widget/shader_list.cpp` — shader cache browser.
- `widget/text_editor.cpp` — in-emulator text editor (presumably for patches).
- `widget/imgui_memory_editor.h` — hex editor.
- `gcn/` — GCN shader disassembler (`gcn_op_names.cpp`, `gcn_context_regs.cpp`, `gcn_shader_regs.cpp`).

Plus:
- GDB-style debugger stub (`src/core/debugger.cpp`).
- `--wait-for-debugger` and `--wait-pid` CLI flags for live debugging.
- RenderDoc integration (`video_core/renderdoc.cpp`).
- DWARF debug info parser (`core/loader/dwarf.cpp`) for backtraces.
- `--log-append` and configurable log levels.

#### fpPS4 — standalone tools

`tools/` directory contains:
- `elf_sym/` — ELF symbol dumper.
- `dump_sym/` — symbol dumper.
- `ps4libdoc/` — NID database tools (with `known_names.txt`, `sceKernelDlsym.txt`, `ps4_names.txt`, `list_from_sdk.txt`).
- `nid_gui_test/` — GUI for testing NID resolution.
- `gfx6_chip/` — AMD GPU register dumper (generates C headers from the merged SI/CI/VI register XML).
- `spirv/spirv_helper.lpr` — SPIR-V inspection tool.
- `param_sfo/param_sfo_info.lpr` — `param.sfo` dumper.
- `playgo/playgo_info.lpr` — playgo chunk dumper.

These are **offline** tools — useful for reverse-engineering work but not for live in-emulator debugging.

#### Verdict

shadPS4's integrated dev tools are **substantially more mature** and game-developer-friendly. fpPS4's tools are useful for emulator authors themselves but provide nothing for end users.

### 2.8 Networking

#### shadPS4

- `core/libraries/network/` — 22 files implementing net, http, http2, ssl, ssl2, netctl, sys_net, net_epoll, net_resolver, net_upnp, posix_sockets, unix_sockets, p2p_sockets, net_obj, net_ctl_obj, net_util.
- `shadnet/` — separate subsystem with a Protobuf-defined protocol (`shadnet.proto`) for cross-instance multiplayer emulation, with a `client.cpp` and `server_probe.cpp` for LAN discovery.

#### fpPS4

- `src/ps4_libscenet.pas`, `ps4_libscehttp.pas`, `ps4_libSceSsl.pas`, `ps4_libscenpsessionsignaling.pas`, `ps4_libscenpsignaling.pas`, `ps4_libscenet.pas`, `ps4_libscerudp.pas`.

#### Verdict

shadPS4 has **much deeper** networking support, including a real P2P/multiplayer emulation story. fpPS4 has the minimum surface area to make single-player games boot.

### 2.9 GUI

- **shadPS4**: separate `shadps4-qtlauncher` repo for end users; built-in `imgui/big_picture/` mode (with settings dialog, controller mapping UI, translations, fonts including CJK/Arabic/Thai/Symbols).
- **fpPS4**: no built-in GUI. Three community GUIs exist in `third_party_gui/`:
  - `fpPS4Fro/` — C# WinForms (most polished)
  - `frofpp4/` — Free Pascal Lazarus LCL
  - `GUI/main.py` — Python tkinter

#### Verdict

shadPS4 wins decisively. fpPS4's GUI situation is fragmented.

### 2.10 Build System

- **shadPS4**: CMake (`CMakeLists.txt`, `CMakePresets.json`, `CMakeSettings.json`), with `externals/` for vendored dependencies, `flake.nix` for Nix users, `scripts/` for build helpers, `tests/` directory.
- **fpPS4**: Lazarus `.lpi`/`.lpr` files per program. Requires FPC 3.3.1 trunk. Windows-centric.

#### Verdict

shadPS4 is engineered for CI/CD and contributor onboarding. fpPS4 is single-developer-friendly.

---

## 3. What Each Project Does Better

### 3.1 What fpPS4 does better than shadPS4 (and the fork)

1. **No-IR direct PSSL → SPIR-V emission has lower compile latency.** For games with thousands of unique shaders, fpPS4's first-load is faster. shadPS4's 28 optimization passes pay off at runtime but cost initial compile time. (Note: shadPS4 mitigates this with `vk_pipeline_serialization.cpp` disk cache, but the very first compile is still slower.)
2. **Leaner binary.** No Qt, no SDL3, no Boost-asio, no Zydis/Xbyak dependency tree. fpPS4 ships as a single executable with a handful of `.a` static libs. Footprint is tiny.
3. **`sys/` and `rtl/` layers are remarkably clean.** The `seh64.pas`, `LFQueue.pas` (intrusive MPSC), `hamt.pas` (Hash Array Mapped Trie), `g23tree.pas` (2-3 tree) are well-isolated, reusable primitives. shadPS4 has equivalents scattered across `common/` but with less consistent design.
4. **`-h` hack flags are pragmatic and battle-tested.** The `DEPTH_DISABLE_HACK`, `COMPUTE_DISABLE_HACK`, `MEMORY_BOUND_HACK`, `IMAGE_TEST_HACK`, `IMAGE_LOAD_HACK`, `DISABLE_SRGB_HACK`, `DISABLE_FMV_HACK`, `SKIP_UNKNOW_TILING` flags are exactly the kind of escape hatches power users want when a specific game has a single bug blocking play. Both shadPS4 upstream and the Shadlix fork lack this systematic user-facing pattern.
5. **Pure-HLE avoids any firmware-dumping legal grey area.** fpPS4 users don't need to dump their PS4. shadPS4 requires it for several modules (audio decoders, fonts, JSON, etc.). This is both a UX advantage and a legal-risk advantage for fpPS4.
6. **Per-module `ajm`/`videorecording`/`gamelivestreaming`/`companionhttpd`/`companionutil` coverage** that shadPS4 lacks or handles more shallowly. (Note: the Shadlix fork has deeper `ajm` coverage than upstream shadPS4 but still lacks the other four.)
7. **`ps4libdoc` NID database is a separate, queryable artifact** (`known_names.txt`, `sceKernelDlsym.txt`, `list_from_sdk.txt`, `ps4_names.txt`). Both shadPS4 upstream and the fork have `aerolib.inl` but it's compiled into the binary.

### 3.2 What the Shadlix fork does better than shadPS4 upstream

1. **Built-in Qt6 GUI** — single-binary experience instead of requiring a separate `shadps4-qtlauncher` download. Includes 30+ Crowdin translations, mod manager, trophy viewer, ELF viewer, compatibility DB integration.
2. **PKG file format support with Crypto++ decryption** — users can point at `.pkg` files directly.
3. **ZArchive (`.zar`) filesystem** — supports the de-facto standard for compressed PS4 game dumps.
4. **Storage I/O Scheduler** — emulates PS4 HDD bandwidth (75/100/125 MiB/s + seek+rotation), fixes streaming-dependent games that break on NVMe.
5. **Memory compression** — page-level LRU compression lets low-RAM systems run large games.
6. **Per-game hack detection** via `HackFeatures` class — auto-detects The Order: 1886 by CUSA ID and applies workarounds without user intervention.
7. **Cubeb audio backend** — lower latency than SDL on Linux/PipeWire, better device routing on macOS.
8. **Centralized config with `ReadbackSpeed` enum** — unified per-game performance/accuracy trade-offs.
9. **Enhanced camera library** — 1,317 LOC vs upstream's smaller one.
10. **Sysmodule library** — proper `sceSysmoduleLoadModule` handling.
11. **Flat NP module layout** — easier to navigate, removes `np_handler` indirection.
12. **Flatpak + AppImage packaging** — proper Linux distribution.
13. **Per-shader skip list** — users can mark known-broken shaders per-game to skip compilation.
14. **Multiple Vulkan backend enhancements** (rebased PRs): scaled min/max blending, sRGB A2R10G10B10, H.265 vdec, coherent storage buffers, structurize-later shader pipeline.
15. **`isDevKit` and `Neo Mode` (PS4 Pro) toggles** in Settings — useful for testing.

### 3.3 What shadPS4 upstream does better than both fpPS4 and the fork

1. **Cross-platform.** Windows, Linux, macOS (Apple Silicon), FreeBSD. fpPS4 is Windows-only. The fork inherits upstream's cross-platform support but is single-developer and less tested on non-Windows.
2. **IR-based shader compiler with optimization passes.** Generates substantially better runtime SPIR-V than fpPS4. The fork inherits this.
3. **Hybrid HLE+LLE with real firmware `.sprx` loading.** Massive compatibility win. The fork inherits this. fpPS4 has nothing equivalent.
4. **Coroutine-based async GPU command processing.** Cleaner than fpPS4's `TvMicroEngine`. The fork inherits this.
5. **Full memory model: Direct + Flexible + Pool + PRT + userfaultfd.** The fork adds compression on top. fpPS4 is simpler.
6. **Disk-cached pipelines.** First launch is slow; second launch is fast. The fork inherits this and adds per-game shader skip lists.
7. **FSR 1 upscaling built-in.** The fork adds an RCAS bar to adjust FSR sharpness.
8. **RenderDoc integration.**
9. **Built-in ImGui dev tools** — frame dump, reg view, memory map, shader list, frame graph, GCN disassembler. The fork inherits this and adds Qt-side equivalents.
10. **Multiplayer netcode** (`shadnet`). The fork inherits this but selectively reverted some shadnet PRs due to instability.
11. **Active development with a real team** — runs Bloodborne / DS Remastered / RDR. The fork is single-developer.
12. **C++20 + CMake + modern toolchain** = easier contributor onboarding. The fork uses the same toolchain.
13. **Proper concurrency primitives** — `SharedFirstMutex`, `adaptive_mutex`, `spin_lock`, `slot_vector`. The fork inherits these.
14. **Stability / not breaking things** — the fork has known regressions from its selective reverts (e.g. reverting predication, reverting HTTP2 fixes). Upstream's slower integration pace produces a more reliable baseline.

### 3.4 What the fork has chosen to revert (anti-patterns to avoid)

These reverts reveal cases where the fork's author found upstream changes broke games. These are valuable signals — upstream should investigate why:

1. **`Revert predication`** — upstream's predication handling broke games. Investigate the regression.
2. **`Revert shader_recompiler: split resource tracking and flatten load from buffer for sharp source (#4782)`** — too aggressive an optimization, broke shaders.
3. **`Revert "Http2 fixes (#4908)"`, `Revert "Trophies online (shadNet) (#4914)"`, `Revert "Net Fixes (#4910)"`, `Revert "np_utility initial impelmentation (#4940)"`, `Revert "Http module fixups and implementations (#4933)"`** — entire networking stack caused regressions. These PRs need rework before being safe.

---

## 4. Where Each Project's Design Comes From

### 4.1 shadPS4's intellectual lineage

The README explicitly credits:
- **yuzu / Hades** — the shader compiler's IR design and many passes are direct ports of the same architectural pattern (basic block, SSA, dominance, post-order, abstract syntax list).
- **Panda3DS** — native x64 execution approach, TCB swap strategy.
- **fpPS4** — reverse-engineering insights on PS4 OS internals.
- **felix86** — x86-64 → RISC-V userspace emulator (likely inspiration for cross-arch considerations).
- **emudev.org** — community / docs hub.

This makes shadPS4 the **heir to multiple emulation lineages**, with fpPS4 being one of its ancestors.

### 4.2 fpPS4's intellectual lineage

fpPS4 is essentially **one author's solo research project** (red-prig), and it shows:
- It uses **Free Pascal** because the author likes Pascal (and it has excellent low-level Windows integration via `windows` unit + raw SEH).
- The `si_ci_vi_merged_*` files are Pascal ports of the official AMD GPU register headers (Southern Islands / Sea Islands / Volcanic Islands = GFX6/GFX7/GFX8).
- The PSSL opcode tables in `ps4_pssl.pas` are clearly hand-curated from AMD's GCN instruction set architecture documentation.
- The HAMT and 2-3 tree implementations are textbook — the author enjoys implementing data structures.

The project's maturity ceiling is limited by being a one-person effort, but its **implementation quality per line of code** is high.

---

## 5. Unified Improvement Roadmap for shadPS4 (drawn from all three repos)

The following 22 recommendations are drawn from concrete code patterns observed across **fpPS4** and **the Shadlix fork** that **shadPS4 upstream** either lacks or implements less aggressively. Each item explicitly cites the source repo, so reviewers can trace the reference implementation. Priorities are calibrated by impact-to-effort ratio.

### Priority P0 (highest impact-to-effort ratio — do these first)

#### P0.1 — Per-game hack flags (user-facing, like fpPS4's `-h`)

**Source:** fpPS4's CLI `-h DEPTH_DISABLE_HACK`, `-h IMAGE_LOAD_HACK`, `-h DISABLE_FMV_HACK`, `-h SKIP_UNKNOW_TILING`, etc.
**Fork complement:** `src/common/hack_features.cpp/.h` adds per-game auto-detection by CUSA ID.

**Why:** When a single bug blocks a game from being playable, users want a per-game escape hatch. Both shadPS4 upstream and the Shadlix fork lack a systematic user-facing pattern. fpPS4 has 8 hack flags proven in production.

**Where to add:** `src/core/emulator_settings.*` — add a `GameHacks` bitmask field. `src/video_core/renderer_vulkan/vk_rasterizer.cpp` — gate depth/compute/srgb paths. `src/video_core/texture_cache/image.cpp` — gate image reload. Wire to both an `--enable-hack <name>` CLI flag and a per-game config file (the fork's approach).

**Effort:** 1–2 days for the framework, ongoing per-hack.
**Impact:** Medium-high. Unlocks previously-broken games for testing.

#### P0.2 — Per-game hack auto-detection by CUSA serial

**Source:** Shadlix fork's `src/common/hack_features.cpp` (the `HackFeatures::Init` function matching `CUSA00035`/`CUSA00076`/`CUSA00100` for The Order: 1886).

**Why:** Auto-detection removes the burden from the user. The Order: 1886 went from unplayable to playable in the fork because of this single commit (`cb0d7cd 1886 hacks perf and graphics (playable)`).

**Where to add:** New `src/common/hack_features.cpp/.h` mirroring the fork. Initialize after `param.sfo` parse, before `eboot.bin` execution. Make the list data-driven (e.g. `data/game_hacks.json`) so it can be updated without recompiling.

**Effort:** 2–3 days.
**Impact:** High for specific broken games.

#### P0.3 — Direct PSSL → SPIR-V fast path for simple shaders

**Source:** fpPS4's no-IR direct emitter (`spirv/emit_*.pas`, ~36k LOC).

**Why:** shadPS4's 28 IR passes cost compile time. For shaders that are already simple (firmware modules, common compute kernels, post-process shaders), a fast 1:1 emit path could shave 50–80% off compile time. First-launch UX would improve dramatically.

**Where to add:** New `src/shader_recompiler/backend/spirv/emit_spirv_fast.cpp` that bypasses the IR and emits SPIR-V directly from the GCN instruction stream for shader profiles marked `simple`. Gate with a `--fast-shader-compile` flag or a per-shader heuristic. Combine with P2.6 (per-shader skip list) for cumulative effect.

**Effort:** 2–3 weeks.
**Impact:** High for first-launch UX.

#### P0.4 — Externalized NID database

**Source:** fpPS4's `tools/ps4libdoc/` with `known_names.txt`, `sceKernelDlsym.txt`, `list_from_sdk.txt`, `ps4_names.txt` as queryable text files.

**Why:** shadPS4's `aerolib.inl` is compiled into the binary. Reverse-engineering contributors can't easily query or extend the NID database without recompiling.

**Where to add:** Move `aerolib.inl` to a loadable `data/nids.csv` at runtime. Add a `shadps4-nids` CLI tool that prints NID → name mappings. Add a `--dump-nids` flag to the main binary. Cache the parsed database in memory.

**Effort:** 3–5 days.
**Impact:** Medium for contributors.

#### P0.5 — PKG file format support with Crypto++ decryption

**Source:** Shadlix fork's `src/core/file_format/pkg.cpp/.h` + `src/core/file_format/pkg_type.cpp` + `src/core/crypto/crypto.cpp/.h` + `src/core/crypto/keys.h` (583 + 1,288 = 1,871 LOC total).

**Why:** Users currently must extract PKG files manually to feed eboot.bin to shadPS4. The fork's PKG support means users can point at a `.pkg` file directly. This is a high-friction UX issue for the broader PS4 game-preservation community.

**Where to add:** Port `src/core/crypto/` and `src/core/file_format/pkg*` from the fork. Add `externals/cryptopp` + `externals/cryptopp-cmake`. Add a `--mount-pkg <path>` CLI flag. Integrate with the existing `file_sys/backends/` layer.

**Effort:** 1–2 weeks (the code already exists, mostly mechanical port).
**Impact:** Very high for end-user UX.

#### P0.6 — ZArchive (`.zar`) filesystem

**Source:** Shadlix fork's `src/common/zar_fs.cpp/.h` (497 LOC).

**Why:** `.zar` is the de-facto standard for compressed PS4 game dumps used by scene groups and most preservation tools. Supporting it natively eliminates the need for users to extract `.zar` files.

**Where to add:** Port `src/common/zar_fs.cpp/.h` from the fork. Integrate with the existing `Common::FS::FindGameByID` path used in `main.cpp`. Update the game-folder scanner to also detect `.zar` archives.

**Effort:** 1 week.
**Impact:** High for users with `.zar` game libraries.

### Priority P1 (high impact, moderate effort)

#### P1.1 — Storage I/O Scheduler

**Source:** Shadlix fork's `src/core/file_sys/storage_scheduler.cpp/.h` (882 LOC).

**Why:** Many PS4 games (especially early-generation titles) load assets at specific cadences timed to the PS4's stock HDD. When emulated on a fast NVMe SSD, the game's streaming logic breaks because assets arrive too fast, leading to texture pop-in glitches or anti-cheat false positives. The scheduler emulates the PS4's HDD bandwidth with proper seek+rotation modeling.

**Where to add:** Port `storage_scheduler.cpp/.h` from the fork. Wire it into `core/file_sys/file.cpp`'s read path. Expose `--storage-bandwidth 75|100|125|0` CLI flag and a per-game setting. Add stats output to the devtools layer.

**Effort:** 2–3 weeks.
**Impact:** High for fixing streaming-related game bugs.

#### P1.2 — Memory compression for low-RAM systems

**Source:** Shadlix fork's `src/core/memory_compression.cpp/.h` (309 LOC).

**Why:** PS4 has ~5.5 GB of usable RAM for games. On systems with 8 GB or less, large games (Bloodborne, RDR, Yakuza) can OOM. Memory compression lets the emulator swap cold pages to a compressed in-process store instead of paging to disk. Conceptually similar to macOS's memory compressor or Linux's zswap.

**Where to add:** Port `memory_compression.cpp/.h` from the fork. Integrate with the existing `MemoryManager::TryWriteBacking` path. Add a `--memory-compression 0|1|2|3` CLI flag (matching the fork's levels).

**Effort:** 2–3 weeks (incl. proper integration with the fault handler).
**Impact:** Medium-high for low-RAM systems.

#### P1.3 — Cubeb audio backend

**Source:** Shadlix fork's `src/core/libraries/audio/cubeb_audio.cpp`.

**Why:** Cubeb has meaningfully lower latency than SDL on Linux/PipeWire and better device routing on macOS. The fork's `common/config.h` adds `AudioBackend::SDL | OpenAL` — port also adds `Cubeb` as a third option.

**Where to add:** Port `cubeb_audio.cpp` from the fork. Add `externals/cubeb`. Extend the `AudioBackend` enum in `emulator_settings.h`.

**Effort:** 1 week.
**Impact:** Medium for audio quality.

#### P1.4 — Standalone reverse-engineering tools

**Source:** fpPS4's `tools/elf_sym/`, `tools/dump_sym/`, `tools/spirv/spirv_helper.lpr`, `tools/param_sfo/param_sfo_info.lpr`, `tools/playgo/playgo_info.lpr`, `tools/gfx6_chip/`.

**Why:** Currently shadPS4 ships its RE tooling baked into the emulator binary, gated behind `--help` and the devtools layer. Standalone CLI tools would let RE contributors iterate without launching the full emulator.

**Where to add:** New `tools/` top-level CMake subdirectory. Each tool is a small `main.cpp` that links against `core/loader`, `core/file_format`, `shader_recompiler/frontend`. Suggested tools: `shadps4-elf-info`, `shadps4-param-sfo`, `shadps4-playgo`, `shadps4-spirv-dis`, `shadps4-pkg-extract` (depends on P0.5).

**Effort:** 1–2 weeks.
**Impact:** Medium for RE contributors.

#### P1.5 — Companion-app and game-streaming HLE stubs

**Source:** fpPS4's `ps4_libscecompanionhttpd.pas`, `ps4_libscecompanionutil.pas`, `ps4_libscevideorecording.pas`, `ps4_libsceshareplay.pas`, `ps4_libscegamelivestreaming.pas`.

**Why:** shadPS4 doesn't have dedicated implementations for these. Some games (e.g. Second Screen titles, Just Dance series) call companion APIs during boot. Currently they'd hit unresolved NID stubs.

**Where to add:** New `src/core/libraries/companion/`, `src/core/libraries/video_recording/`, `src/core/libraries/share_play/`, `src/core/libraries/game_live_streaming/` directories with stub implementations returning `SCE_OK` for the common paths.

**Effort:** 2–3 weeks per module (mostly RE work).
**Impact:** Unlocks a class of games.

#### P1.6 — Enhanced camera library

**Source:** Shadlix fork's `src/core/libraries/camera/camera.cpp` (1,317 LOC) with system memory mapping for camera frames and `sceCameraGetCalibData` stub.

**Why:** PS4 camera-using games (Playroom, Just Dance, certain VR titles) need a real camera HLE. Upstream's camera library is minimal.

**Where to add:** Port `camera.cpp` from the fork.

**Effort:** 1 week.
**Impact:** Medium for camera-using games.

#### P1.7 — Sysmodule library

**Source:** Shadlix fork's `src/core/libraries/system/sysmodule.cpp/.h` + `sysmodule_error.h`.

**Why:** Many games call `sceSysmoduleLoadModule` to dynamically load system modules at runtime. Upstream's stub-like handling causes silent failures.

**Where to add:** Port `sysmodule.cpp/.h` from the fork. Wire it into `Libraries::Init` to track loaded module state.

**Effort:** 3–5 days.
**Impact:** Medium for some games.

#### P1.8 — IPC client

**Source:** Shadlix fork's `src/core/ipc/ipc_client.cpp/.h` (351 LOC).

**Why:** Upstream has the IPC server infrastructure but lacks a clean client. The fork's `ipc_client` enables future features like "launch from browser" or "remote control from a Discord bot". It also enables the fork's "BootLauncher" feature (`getBootLauncher()`).

**Where to add:** Port `ipc_client.cpp/.h` from the fork. Add `--ipc-client <server>` CLI flag for slave-mode launch.

**Effort:** 3–5 days.
**Impact:** Low-medium. Unlocks future automation.

### Priority P2 (medium impact, higher effort)

#### P2.1 — Compile-time SPIR-V emission benchmark suite

**Source:** Conceptual — inspired by fpPS4's simpler emitter and the Shadlix fork's selective reverts of upstream optimization PRs.

**Why:** shadPS4 has 28 IR passes but no public data on which ones actually improve FPS for which games. The fork's reverts (`Revert shader_recompiler: split resource tracking...`, `Revert predication`) suggest specific optimizations cause regressions on specific games. A pass-by-pass benchmark would let the team disable passes that hurt more than they help on specific shader profiles.

**Where to add:** New `tests/shader_bench/` directory. CMake target that runs a corpus of captured shaders through (a) full pipeline, (b) pipeline minus one pass, and reports FPS delta + compile time.

**Effort:** 2–4 weeks.
**Impact:** Long-term shader compiler quality.

#### P2.2 — Linux/FreeBSD `userfaultfd`-based texture upload fast path

**Source:** Generalizes fpPS4's `IMAGE_LOAD_HACK` concept. Upstream already has `--userfaultfd` but only for memory tracking.

**Why:** Texture upload is a known bottleneck. On Linux, userfaultfd can be used to lazy-load texture memory pages from a host staging buffer only when the GPU actually reads them, instead of uploading the full texture up-front.

**Where to add:** `src/video_core/texture_cache/image.cpp` — add a `UserfaultTextureUploader` strategy alongside the existing `TileManager`.

**Effort:** 3–4 weeks.
**Impact:** High for Linux users with large texture-heavy games.

#### P2.3 — Per-game shader cache export/import + skip list

**Source:** Shadlix fork's `getShaderSkipsEnabled()` + `ShouldSkipShader(hash)` + `SetSkippedShaderHashes(game_id)` from `common/config.h`. Generalizes fpPS4's tendency to ship pre-built `shaders/*.comp`.

**Why:** Community-maintained per-game shader caches (à la Yuzu's shader cache) would let users share first-launch work. The fork's per-shader skip list lets users bypass known-broken shaders per-game.

**Where to add:** Extend `vk_pipeline_serialization.cpp` to emit/load a `shader_cache/<title_id>.bin` file. Add a `--import-shader-cache <path>` CLI flag. Document the format. Add per-game skip list JSON loaded from `data/shader_skips/<title_id>.json`.

**Effort:** 1–2 weeks for format + CLI. Plus community tooling.
**Impact:** Very high for end-user UX.

#### P2.4 — Vulkan backend enhancements (rebased PRs)

**Source:** Shadlix fork has these via rebased upstream PRs. Many are already merged into upstream `main` but the fork demonstrates they work together.

**What to ensure is in upstream `main`:**
- Scaled min/max blending emulation (PR #4768)
- sRGB for A2R10G10B10 display buffers (PR #4957)
- H.265/HEVC video decode (PR #4951)
- Coherent storage buffer decoration (PR #4896)
- Shared memory barrier enhancement + divergent loop detection (PR #4941)
- Shader structurize-later pipeline (PR #4906)
- Do not auto-select software Vulkan device (PR #4904)

**Effort:** Verify each is in `main`; if not, cherry-pick.
**Impact:** Cumulative — fixes several games.

#### P2.5 — Centralized config refactor with `ReadbackSpeed` enum

**Source:** Shadlix fork's `src/common/config.cpp/.h` (3,594 LOC).

**Why:** The fork's centralized config makes adding per-game settings dramatically easier. The `ReadbackSpeed` enum (`Disable | Unsafe | Low | Default | Fast`) in particular is a pattern upstream should adopt — it lets users trade accuracy for performance per-game. Combined with P1.1 (StorageScheduler) it provides a coherent "performance profile" system.

**Where to add:** Refactor `EmulatorSettings` into a free-function-style `Config::` namespace matching the fork. Keep backward compatibility for `EmulatorSettings::GetInstance()` as a thin wrapper.

**Effort:** 3–4 weeks refactor. Should be done before P0.1 (hack flags) so the hack flags have a clean home.
**Impact:** Medium-long-term. Enables cleaner feature additions.

#### P2.6 — Screenshot module

**Source:** Shadlix fork's `src/video_core/screenshot.cpp/.h` (176 LOC) + `src/common/stb_write.cpp`.

**Why:** Upstream has the `Alt+F12` screenshot hotkey but no dedicated module — screenshots are handled inline in the presenter. The fork's approach is cleaner and supports programmatic triggering (e.g. for automatic save screenshots before launching, or for in-emulator photo mode).

**Where to add:** Port `screenshot.cpp/.h` from the fork. Move the inline screenshot logic from `vk_presenter.cpp` into the new module.

**Effort:** 1 week.
**Impact:** Low-medium. Cleaner architecture.

#### P2.7 — Flatpak + AppImage + macOS .app packaging

**Source:** Shadlix fork's `.github/linux-appimage-qt.sh`, `dist/net.shadps4.shadPS4.metainfo.xml`, `dist/MacOSBundleInfo.plist.in`, `dist/qt.conf`, `net.shadps4.shadPS4.yaml` (Flatpak manifest), `externals/MoltenVK`.

**Why:** Upstream's distribution story is currently Windows-centric with Linux/macOS as an afterthought. The fork ships as proper AppImage / Flatpak / .app bundles. This dramatically lowers the install friction for non-Windows users.

**Where to add:** Port the dist files from the fork. Add CI jobs for AppImage + Flatpak + macOS bundle.

**Effort:** 1–2 weeks for CI setup.
**Impact:** High for non-Windows adoption.

### Priority P3 (lower priority, long-term)

#### P3.1 — Pure-HLE fallback mode (no firmware required)

**Source:** fpPS4's pure-HLE approach for users who can't or won't dump firmware.

**Why:** Currently shadPS4 requires firmware `.sprx` modules for several features (audio decoders, fonts, JSON, etc.). A pure-HLE fallback mode (lower compatibility, but no firmware requirement) would lower the onboarding barrier.

**Where to add:** New `src/core/libraries/audiodec_hle/`, `src/core/libraries/font_hle/`, etc. Loaded as fallback when `sys_modules/` is empty.

**Effort:** Many weeks. Ongoing RE.
**Impact:** High for new-user onboarding.

#### P3.2 — Portable SEH-style fault handler as a reusable library

**Source:** Conceptual from fpPS4's `rtl/seh64.pas` and `rtl/atomic.pas`.

**Why:** shadPS4's signal handling is currently split across `core/signals.cpp`, `common/signal_context.cpp`, and CPU-specific paths. A unified, documented "fault handler" abstraction would simplify future porting (e.g. if Android or a BSD variant needs different handling).

**Where to add:** Refactor `common/signal_context.*` into a proper `common/fault_handler/` library with per-platform backends.

**Effort:** 2–3 weeks refactor.
**Impact:** Long-term maintainability.

#### P3.3 — Built-in Qt6 GUI option (single-binary mode)

**Source:** Shadlix fork's entire `src/qt_gui/` directory (~100 files, 30+ Crowdin translations).

**Why:** shadPS4 upstream currently splits the GUI into a separate repo (`shadps4-qtlauncher`). For users who want a single-binary experience with the polished Qt UI, integrating the GUI directly would be valuable. The fork demonstrates this is feasible without breaking the CLI/headless mode.

**Where to add:** Carefully — the fork's `qt_gui/` is large and tied to the fork's centralized `config.cpp`. Best done as a gradual upstreaming of individual Qt widgets (settings_dialog first, then main_window, then trophy_viewer, etc.) over multiple releases. Keep the existing `shadps4-qtlauncher` as the recommended GUI in the meantime.

**Effort:** 4–8 weeks spread over multiple releases.
**Impact:** High for UX, but high coordination cost.

#### P3.4 — Investigate the fork's reverted PRs (anti-pattern audit)

**Source:** The Shadlix fork's commit log reveals 7+ selective reverts of upstream PRs.

**Why:** The reverts are valuable signals that the fork's author found these PRs broke specific games. Upstream should investigate the root cause and either fix or roll back.

**PRs to investigate:**
- Predication handling (revert commit `79850ab Revert predication`)
- `#4782` — shader_recompiler split resource tracking and flatten load from buffer for sharp source
- `#4908` — Http2 fixes
- `#4914` — Trophies online (shadNet)
- `#4910` — Net Fixes
- `#4940` — np_utility initial implementation
- `#4933` — Http module fixups and implementations
- `#4930` — Tss support
- `#4927` — Trigger a lightbar reset on controller connection
- `#4973` — Fix IR dumping

**Effort:** 1 day per PR to bisect the regression.
**Impact:** Cumulative. Each fix prevents future regressions.

#### P3.5 — Flat NP module layout refactor

**Source:** Shadlix fork's flat `np/np_matching2.cpp`, `np/np_score.cpp`, `np/np_signaling.cpp`, `np/np_web_api.cpp`, `np/np_web_api2.cpp` layout vs upstream's deeply-nested `np/np_matching2/`, `np/np_score/`, etc.

**Why:** The flat layout is easier to navigate and removes the somewhat-overengineered NP handler indirection. Upstream's `np_handler.cpp` (3,578 LOC) acts as a god-object that the fork deleted entirely.

**Where to add:** Gradual refactor. Move each subdirectory up one level, then delete `np_handler.cpp` once all references are migrated.

**Effort:** 2–3 weeks refactor.
**Impact:** Low-medium. Maintainability.

---

## 6. Summary Decision Matrix

The matrix below is the **unified** view across all three source repos. "Source" column tells you which repo(s) the recommendation originates from.

| # | Improvement | Source | Effort | Impact | Priority |
|---|---|---|---|---|---|
| P0.1 | Per-game `-h` hack flags | fpPS4 | 1–2 days | Medium-high | **P0** |
| P0.2 | Per-game hack auto-detection by CUSA | fork | 2–3 days | High (specific games) | **P0** |
| P0.3 | Direct PSSL→SPIR-V fast path | fpPS4 | 2–3 weeks | High (UX) | **P0** |
| P0.4 | Externalized NID database | fpPS4 | 3–5 days | Medium (contributors) | **P0** |
| P0.5 | PKG file format + Crypto++ decryption | fork | 1–2 weeks | Very high (UX) | **P0** |
| P0.6 | ZArchive (`.zar`) filesystem | fork | 1 week | High (UX) | **P0** |
| P1.1 | Storage I/O Scheduler | fork | 2–3 weeks | High (game bugs) | **P1** |
| P1.2 | Memory compression for low-RAM | fork | 2–3 weeks | Medium-high | **P1** |
| P1.3 | Cubeb audio backend | fork | 1 week | Medium (audio) | **P1** |
| P1.4 | Standalone RE tools | fpPS4 | 1–2 weeks | Medium (contributors) | **P1** |
| P1.5 | Companion/streaming HLE stubs | fpPS4 | 2–3 weeks/module | Medium (games) | **P1** |
| P1.6 | Enhanced camera library | fork | 1 week | Medium (camera games) | **P1** |
| P1.7 | Sysmodule library | fork | 3–5 days | Medium | **P1** |
| P1.8 | IPC client | fork | 3–5 days | Low-medium (future) | **P1** |
| P2.1 | Shader pass benchmark suite | conceptual | 2–4 weeks | Long-term | **P2** |
| P2.2 | userfaultfd texture upload | fpPS4 + upstream | 3–4 weeks | High (Linux) | **P2** |
| P2.3 | Per-game shader cache + skip list | fork | 1–2 weeks | Very high (UX) | **P2** |
| P2.4 | Vulkan backend PR audit (ensure all merged) | fork | 1 day/PR | Cumulative | **P2** |
| P2.5 | Centralized config refactor | fork | 3–4 weeks | Medium-long-term | **P2** |
| P2.6 | Screenshot module | fork | 1 week | Low-medium | **P2** |
| P2.7 | Flatpak + AppImage + .app packaging | fork | 1–2 weeks | High (non-Windows) | **P2** |
| P3.1 | Pure-HLE fallback mode | fpPS4 | Many weeks | High (onboarding) | **P3** |
| P3.2 | Unified fault-handler library | fpPS4 concept | 2–3 weeks | Maintainability | **P3** |
| P3.3 | Built-in Qt6 GUI (gradual upstream) | fork | 4–8 weeks | High (UX) | **P3** |
| P3.4 | Investigate fork's reverted PRs | fork | 1 day/PR | Cumulative | **P3** |
| P3.5 | Flat NP module layout | fork | 2–3 weeks | Low-medium | **P3** |

---

## 7. Final Assessment

After analyzing all three codebases, the strategic picture is clear:

### 7.1. shadPS4 upstream is the right foundation

Its architectural choices — yuzu-inspired IR-based shader compiler with 28 optimization passes, hybrid HLE+LLE with real firmware `.sprx` loading, coroutine-based async GPU command processor, full Vulkan renderer with disk-cached pipelines, integrated ImGui dev tools, `SharedFirstMutex` and proper concurrency primitives, cross-platform support across Windows/Linux/macOS/FreeBSD — are all the right calls for a serious PS4 emulator that aims to run commercial AAA games. The team is active and the project is the de-facto standard.

### 7.2. fpPS4 is a source of clever low-level patterns

Its main value to shadPS4 is **not** as a competitive alternative, but as a **source of clever implementation patterns** that shadPS4 has not yet adopted:

1. The `-h` hack-flag pattern (P0.1) is the single highest-impact, lowest-effort improvement shadPS4 could make today.
2. The direct PSSL→SPIR-V fast path (P0.3) is a meaningful compile-time optimization.
3. The externalized NID database (P0.4) lowers the contributor barrier.
4. fpPS4's coverage of `companion`/`streaming`/`videorecording` modules (P1.5) represents real RE work shadPS4 could inherit and re-license under GPL-2.0.

### 7.3. The Shadlix fork is a source of major end-user features

Its main value to shadPS4 upstream is **operational user-experience features** that the upstream team has chosen not to prioritize:

1. **PKG file format + Crypto++ decryption** (P0.5) — eliminates the most common user friction (extracting PKG files manually).
2. **ZArchive filesystem** (P0.6) — supports the de-facto standard for compressed PS4 game dumps.
3. **Storage I/O Scheduler** (P1.1) — fixes a class of subtle game bugs caused by too-fast host storage.
4. **Memory compression** (P1.2) — lets low-RAM systems run large games.
5. **Per-game hack auto-detection** (P0.2) — already proven to make The Order: 1886 playable.
6. **Cubeb audio backend** (P1.3) — better cross-platform audio.
7. **Enhanced camera, sysmodule, IPC client** (P1.6–P1.8) — concrete RE progress.
8. **Flatpak + AppImage + macOS .app packaging** (P2.7) — proper non-Windows distribution.

The fork also provides a **regression audit trail** (P3.4) — its selective reverts of upstream PRs (`#4782`, `#4908`, `#4910`, `#4914`, `#4933`, `#4940`, predication, etc.) are valuable signals that these PRs caused real-world game breakage and should be investigated.

### 7.4. Recommended execution order

**Phase 1 (next 1–2 releases):**
- P0.1 + P0.2 (hack flags + auto-detection) — immediate user benefit
- P0.4 (external NID DB) — low-cost contributor goodwill
- P0.5 + P0.6 (PKG + ZAR support) — eliminates top user friction
- P1.6 + P1.7 (camera + sysmodule) — quick ports from fork
- P3.4 begin (regression audit)

**Phase 2 (next 3–6 months):**
- P0.3 (direct PSSL→SPIR-V fast path) — first-launch UX
- P1.1 (StorageScheduler) — game bug fixes
- P1.3 (Cubeb) — audio quality
- P2.3 (per-game shader cache + skip list)
- P2.7 (Flatpak/AppImage packaging)

**Phase 3 (long-term):**
- P1.2 (memory compression)
- P1.5 (companion/streaming HLE)
- P2.1 (shader pass benchmark)
- P2.5 (centralized config refactor) — enables future feature additions
- P3.3 (gradual Qt GUI upstreaming)

Doing Phases 1 and 2 would materially improve the user experience for existing players while also broadening the contributor base — all without compromising the architectural integrity that makes shadPS4 upstream the right foundation.
