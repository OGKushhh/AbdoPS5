#include "emulator.h"
#include "graphics/guest_gpu/pm4Dump.h"

#include "common/abi.h"
#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "common/file.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "common/singleton.h"
#include "common/stringUtils.h"
#include "common/subsystems.h"
#include "common/systemInfo.h"
#include "common/threads.h"
#include "graphics/host_gpu/iommu.h"
#include "graphics/host_gpu/tmrController.h"
#include "graphics/presentation/screenshot.h"
#include "graphics/presentation/window.h"
#include "ipc/ipcClient.h"
#include "kernel/fileSystem.h"
#include "kernel/memoryCompression.h"
#include "kernel/memory.h"
#include "kernel/pthread.h"
#include "kernel/storageScheduler.h"
#include "kytyGitVersion.h"
#include "libs/agc.h"
#include "libs/audio.h"
#include "libs/controller.h"
#include "libs/libs.h"
#include "libs/network.h"
#include "loader/runtimeLinker.h"
#include "loader/systemContent.h"
#include "loader/timer.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

namespace Emulator {

static void PrintSystemInfo() {
        const Common::SystemInfo info = Common::GetSystemInfo();

#if defined(__APPLE__)
        static constexpr auto platform_name = "macOS";
#elif KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
        static constexpr auto platform_name = "Windows";
#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX
        static constexpr auto platform_name = "Linux";
#else
        static constexpr auto platform_name = "Unknown";
#endif

        LOGF("Build\n"
             "  version: %s\n\n"
             "Host\n"
             "  os:      %s\n"
             "  cpu:     %s\n"
             "  threads: %u\n\n",
             KYTY_BUILD_LABEL, platform_name, info.ProcessorName.c_str(),
             std::thread::hardware_concurrency());
}

static void KytyClose() {
        // Kyty-039: flush + close the PM4 dump file.
        Pm4Dump::Shutdown();

        auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();

        rt->Clear();

        LOGF("done!\n");

        Common::Subsystems::EmergencyShutdownActive();
}

static void MountOrCreateDir(const std::filesystem::path& dir, const std::string& point) {
        if (!Common::File::IsDirectoryExisting(dir)) {
                Common::File::CreateDirectories(dir);
        }

        EXIT_NOT_IMPLEMENTED(!Common::File::IsDirectoryExisting(dir));

        Libs::LibKernel::FileSystem::Mount(dir, point);
        auto dir_text = Common::PathToString(dir);
        LOGF("Mounted %s -> %s\n", point.c_str(), dir_text.c_str());
}

static void MountSandboxDirs() {
        std::string title_id;
        if (!Loader::SystemContentParamSfoGetString("TITLE_ID", &title_id) || title_id.empty()) {
                title_id = "UNKNOWN";
        }

        MountOrCreateDir("_DownloadData/" + title_id, "/download0");
        MountOrCreateDir("_TempData/" + title_id, "/temp0");
        MountOrCreateDir("_TempData/" + title_id, "/temp");
}

static bool ClearDirectoryContents(const std::filesystem::path& dir) {
        bool ok = true;

        for (const auto& entry: Common::File::GetDirEntries(dir)) {
                if (entry.name == "." || entry.name == "..") {
                        continue;
                }

                auto path = dir / entry.name;

                if (entry.is_file) {
                        Common::File::RemoveReadonly(path);
                        ok = Common::File::DeleteFile(path) && ok;
                } else {
                        ok = ClearDirectoryContents(path) && ok;
                        ok = Common::File::DeleteDirectory(path) && ok;
                }
        }

        return ok;
}

static void ClearDebugTextureFolder() {
        const std::string debug_texture_folder = "_Textures";

        if (!Common::File::IsDirectoryExisting(debug_texture_folder)) {
                Common::File::CreateDirectories(debug_texture_folder);
                return;
        }

        if (!ClearDirectoryContents(debug_texture_folder)) {
                LOGF_COLOR(Log::Color::BrightYellow, "TextureDump: failed to completely clear %s\n",
                           debug_texture_folder.c_str());
        }
}

static void Init(const Config::ConfigOptions& cfg, const std::filesystem::path& param_json,
                 Common::Subsystems& subsystems) {
        EXIT_IF(!Common::Thread::IsMainThread());

        subsystems.Initialize<Config::Lifecycle>();
        Config::Load(cfg);
        subsystems.Initialize<Log::Lifecycle>();

        if (Common::File::IsFileExisting(param_json)) {
                Loader::SystemContentLoadParamSfo(param_json);
                if (const auto flexible_memory_size = Loader::SystemContentGetFlexibleMemorySize();
                    flexible_memory_size != 0) {
                        Libs::LibKernel::Memory::SetFlexibleMemorySize(flexible_memory_size);
                }
        }

        // Initialization order is explicit; destruction is automatic and reversed.
        subsystems.Initialize<Loader::Timer::Lifecycle>();
        subsystems.Initialize<Libs::LibKernel::PthreadLifecycle>();
        subsystems.Initialize<Profiler::Lifecycle>();
        subsystems.Initialize<Libs::Network::Lifecycle>();
        subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
        subsystems.Initialize<Libs::LibKernel::FileSystem::Lifecycle>();
        subsystems.Initialize<Libs::Controller::Lifecycle>();
        subsystems.Initialize<Libs::Audio::Lifecycle>();
        subsystems.Initialize<Libs::Graphics::Lifecycle>();

        // Kyty-016: Initialize the AMD IOMMU model and register it with
        // the central MMIO dispatcher. Done AFTER graphics lifecycle so
        // the MMIO dispatcher (which the Iommu registers with) is ready.
        // The Iommu is a singleton — see src/graphics/host_gpu/iommu.h.
        Libs::Graphics::InitializeIommu();

        // Kyty-017: Initialize the TMR (Trust Memory Range) controller.
        // All entries default to permissive (0x3F07) — the emulator
        // never blocks any memory access due to TMR (the host OS already
        // enforces memory protection).
        Libs::Graphics::InitializeTmr();

        // Kyty-022: Initialize the screenshot module.
        Libs::Graphics::InitializeScreenshot();

        // Kyty-023: Initialize the IPC server for external automation.
        // Listens on localhost:28015. Can be disabled with --no-ipc.
        if (!cfg.no_ipc) {
                Libs::Ipc::InitializeIpcServer(28015);
        }

        // Kyty-016: Register a store callback so the IOMMU's
        // COMPLETION_WAIT_STORE command can write 8 bytes to any
        // physical address in RAM. The PS5 kernel uses this as a
        // privileged-write primitive (e.g. to clear NESTED_CTRL bits
        // in VMCBs after HV escape).
        //
        // The callback translates physical address → host virtual address
        // using the kernel's backing store: host_va = base + pa.
        // If the PA is outside RAM, the write fails (returns false).
        if (auto* iommu = Libs::Graphics::GetIommu()) {
                auto store_callback = +[](uint64_t pa, uint64_t value, void* /*user_data*/) -> bool {
                        const uint64_t base = Libs::LibKernel::Memory::GetPhysicalMemoryBase();
                        const uint64_t size = Libs::LibKernel::Memory::GetPhysicalMemorySize();
                        if (base == 0 || size == 0) {
                                return false; // backing store not initialized
                        }
                        if (pa + sizeof(uint64_t) > size) {
                                return false; // PA outside RAM
                        }
                        // Compute host VA and write 8 bytes.
                        auto* host_ptr = reinterpret_cast<uint64_t*>(base + pa);
                        *host_ptr = value;
                        return true;
                };
                iommu->RegisterStoreCallback(store_callback, nullptr);

                // Kyty-016: Register a read callback so Translate() can
                // walk the device table + page tables. Same PA→host VA
                // translation as the store callback, but reads instead.
                auto read_callback = +[](uint64_t pa, void* dst, size_t size, void* /*user_data*/) -> bool {
                        const uint64_t base = Libs::LibKernel::Memory::GetPhysicalMemoryBase();
                        const uint64_t mem_size = Libs::LibKernel::Memory::GetPhysicalMemorySize();
                        if (base == 0 || mem_size == 0) {
                                return false;
                        }
                        if (pa + size > mem_size) {
                                return false;
                        }
                        std::memcpy(dst, reinterpret_cast<const void*>(base + pa), size);
                        return true;
                };
                iommu->RegisterReadCallback(read_callback, nullptr);
        }

        // Kyty-009: Configure the storage I/O scheduler.
        const auto storage_bw = Config::GetStorageBandwidthMbps();
        if (storage_bw != 0) {
                Libs::LibKernel::FileSystem::GetStorageScheduler().Configure(storage_bw);
        }

        // Kyty-010: Configure memory compression for low-RAM systems.
        const auto mem_comp = Config::GetMemoryCompressionLevel();
        if (mem_comp > 0) {
                Libs::LibKernel::Memory::GetMemoryCompression().SetCompressionLevel(mem_comp);
        }
}

static void LoadElf(const std::filesystem::path& elf, bool dbg_print_reloc = false,
                    const std::filesystem::path& save_name = {}) {
        auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();

        auto* program = rt->LoadProgram(
            Libs::LibKernel::FileSystem::GetRealFilename(Common::PathToGenericString(elf)));

        if (dbg_print_reloc) {
                program->dbg_print_reloc = true;
        }

        if (!save_name.empty()) {
                rt->SaveProgram(program, Libs::LibKernel::FileSystem::GetRealFilename(
                                             Common::PathToGenericString(save_name)));
        }
}

static void Execute(const std::filesystem::path& game_patch) {
        auto           patch_path = game_patch;
        Common::Thread guest_thread(
            [](void* param) {
                    auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();
                    rt->Execute(*static_cast<const std::filesystem::path*>(param));
            },
            &patch_path);
        Libs::Graphics::WindowRun();
        std::quick_exit(0);
}

void Run(const RunOptions& options) {
        if (options.app0_dir.empty()) {
                EXIT("app0 directory is required\n");
        }

        if (options.elf.empty()) {
                EXIT("ELF is required\n");
        }

        const auto         param_json = options.app0_dir / "sce_sys" / "param.json";
        Common::Subsystems subsystems(true);
        Init(options.config, param_json, subsystems);

        ClearDebugTextureFolder();

        PrintSystemInfo();
        std::string title_id;
        if (Loader::SystemContentParamSfoGetString("TITLE_ID", &title_id) && !title_id.empty()) {
                Log::WriteToConsoleAndLog(fmt::format("Title ID: {}\n", title_id));
        }

        int ok = atexit(KytyClose);
        EXIT_NOT_IMPLEMENTED(ok != 0);

        // Guest threads are still running, so skip KytyClose() and only flush emergency state.
        ok = at_quick_exit(Common::Subsystems::EmergencyShutdownActive);
        EXIT_NOT_IMPLEMENTED(ok != 0);

        Libs::LibKernel::FileSystem::Mount(options.app0_dir, "/app0");
        Libs::LibKernel::FileSystem::Mount(options.app0_dir, "/hostapp");

        MountSandboxDirs();

        auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();
        Libs::InitAll(rt->Symbols());
        // Kyty-039: initialize the PM4 dump tool if enabled in config.
        if (Config::Pm4DumpEnabled()) {
                if (!Pm4Dump::Initialize(Config::GetPm4DumpPath())) {
                        LOGF_COLOR(Log::Color::BrightYellow, "Kyty-039: failed to open PM4 dump file: %s\n",
                                   Common::PathToString(Config::GetPm4DumpPath()).c_str());
                } else {
                        LOGF("Kyty-039: PM4 dump enabled, writing to %s\n",
                             Common::PathToString(Config::GetPm4DumpPath()).c_str());
                }
        }

        LoadElf(options.elf);

        Execute(options.game_patch);
}

} // namespace Emulator
