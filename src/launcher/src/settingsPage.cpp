#include "settingsPage.h"
#include "inputTab.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QVBoxLayout>

// Kyty-UI: Redesigned settings page implementation.
// 5 tabs with real controls for every Configuration field.
// No .ini editing — everything is a toggle, slider, or dropdown.

namespace {

// Dark theme QSS for the settings page — matches Design A mockup.
constexpr const char* kSettingsQss = R"(
    QListWidget#settingsSidebar {
        background: #1a1a2e;
        border: none;
        color: #8899aa;
        font-size: 13px;
        padding: 8px 0px;
        outline: none;
    }
    QListWidget#settingsSidebar::item {
        padding: 10px 20px;
        border-left: 3px solid transparent;
    }
    QListWidget#settingsSidebar::item:selected {
        background: #16213e;
        color: #1a9fff;
        border-left: 3px solid #1a9fff;
    }
    QListWidget#settingsSidebar::item:hover {
        background: #16213e;
        color: #ccddee;
    }
    QScrollArea {
        background: #0f0f1a;
        border: none;
    }
    QWidget#tabContent {
        background: #0f0f1a;
    }
    QLabel {
        color: #ccddee;
        font-size: 13px;
    }
    QLabel#sectionTitle {
        color: #1a9fff;
        font-size: 15px;
        font-weight: bold;
        padding-bottom: 8px;
    }
    QLabel#settingLabel {
        color: #aabbcc;
        font-size: 12px;
    }
    QLabel#settingDesc {
        color: #667788;
        font-size: 11px;
        padding-left: 4px;
    }
    QCheckBox {
        color: #ccddee;
        font-size: 13px;
        spacing: 8px;
    }
    QCheckBox::indicator {
        width: 18px;
        height: 18px;
        border: 2px solid #334455;
        border-radius: 4px;
        background: #1a1a2e;
    }
    QCheckBox::indicator:checked {
        background: #1a9fff;
        border-color: #1a9fff;
        image: none;
    }
    QCheckBox::indicator:hover {
        border-color: #1a9fff;
    }
    QComboBox {
        background: #1a1a2e;
        color: #ccddee;
        border: 1px solid #334455;
        border-radius: 4px;
        padding: 6px 12px;
        font-size: 13px;
        min-width: 200px;
    }
    QComboBox:hover {
        border-color: #1a9fff;
    }
    QComboBox::drop-down {
        border: none;
        width: 24px;
    }
    QComboBox::down-arrow {
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 6px solid #8899aa;
        margin-right: 8px;
    }
    QComboBox QAbstractItemView {
        background: #1a1a2e;
        color: #ccddee;
        border: 1px solid #334455;
        selection-background-color: #16213e;
        selection-color: #1a9fff;
        outline: none;
    }
    QSlider::groove:horizontal {
        height: 6px;
        background: #334455;
        border-radius: 3px;
    }
    QSlider::handle:horizontal {
        width: 16px;
        height: 16px;
        background: #1a9fff;
        border-radius: 8px;
        margin: -5px 0;
    }
    QSlider::handle:horizontal:hover {
        background: #4db8ff;
    }
    QSpinBox {
        background: #1a1a2e;
        color: #ccddee;
        border: 1px solid #334455;
        border-radius: 4px;
        padding: 6px 8px;
        font-size: 13px;
        min-width: 100px;
    }
    QSpinBox:hover {
        border-color: #1a9fff;
    }
    QFrame#separator {
        background: #1a1a2e;
        max-height: 1px;
    }
)";

// Common console languages for the dropdown
const QStringList kConsoleLanguages = {
    "Japanese", "English US", "English UK", "French", "Spanish",
    "German", "Italian", "Dutch", "Portuguese", "Brazilian Portuguese",
    "Russian", "Korean", "Traditional Chinese", "Simplified Chinese",
    "Finnish", "Swedish", "Danish", "Norwegian", "Polish", "Turkish",
    "Greek", "Czech", "Hungarian", "Romanian", "Bulgarian",
    "Croatian", "Serbian", "Slovak", "Thai", "Vietnamese",
};

} // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    SetupUi();
}

void SettingsPage::SetupUi() {
    setStyleSheet(kSettingsQss);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Settings sidebar (tab selector) ---
    auto* sidebar = new QListWidget(this);
    sidebar->setObjectName("settingsSidebar");
    sidebar->setFixedWidth(200);
    sidebar->setFocusPolicy(Qt::NoFocus);
    new QListWidgetItem("Graphics", sidebar);
    new QListWidgetItem("Audio", sidebar);
    new QListWidgetItem("Input", sidebar);
    new QListWidgetItem("Advanced", sidebar);
    new QListWidgetItem("Hacks", sidebar);
    sidebar->setCurrentRow(0);
    mainLayout->addWidget(sidebar);

    // --- Tab content (stacked) ---
    m_tabs = new QStackedWidget(this);
    m_tabs->addWidget(CreateGraphicsTab());
    m_tabs->addWidget(CreateAudioTab());
    // Input tab — controller diagram + button mapping + analog settings
    m_tabs->addWidget(new InputTab(m_tabs));
    m_tabs->addWidget(CreateAdvancedTab());
    m_tabs->addWidget(CreateHacksTab());
    mainLayout->addWidget(m_tabs, 1);

    connect(sidebar, &QListWidget::currentRowChanged,
            m_tabs, &QStackedWidget::setCurrentIndex);
}

QWidget* SettingsPage::CreateToggleRow(QCheckBox*& checkbox, const QString& label,
                                        const QString& tooltip) {
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 4);
    checkbox = new QCheckBox(label, container);
    checkbox->setToolTip(tooltip);
    layout->addWidget(checkbox);
    layout->addStretch();
    return container;
}

QWidget* SettingsPage::CreateDropdownRow(QComboBox*& combo, const QString& label,
                                           const QStringList& items, const QString& tooltip) {
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 4);

    auto* lbl = new QLabel(label, container);
    lbl->setObjectName("settingLabel");
    lbl->setToolTip(tooltip);
    layout->addWidget(lbl);
    layout->addStretch();

    combo = new QComboBox(container);
    combo->addItems(items);
    combo->setToolTip(tooltip);
    layout->addWidget(combo);

    return container;
}

QWidget* SettingsPage::CreateSliderRow(QSlider*& slider, QLabel*& value_label,
                                        const QString& label, int min, int max,
                                        int default_val, const QString& tooltip) {
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 4);

    auto* lbl = new QLabel(label, container);
    lbl->setObjectName("settingLabel");
    lbl->setToolTip(tooltip);
    lbl->setMinimumWidth(180);
    layout->addWidget(lbl);

    slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(min, max);
    slider->setValue(default_val);
    slider->setToolTip(tooltip);
    layout->addWidget(slider, 1);

    value_label = new QLabel(QString::number(default_val), container);
    value_label->setObjectName("settingLabel");
    value_label->setMinimumWidth(50);
    layout->addWidget(value_label);

    QObject::connect(slider, &QSlider::valueChanged, value_label,
                     [value_label](int val) { value_label->setText(QString::number(val)); });

    return container;
}

QWidget* SettingsPage::CreateSpinRow(QSpinBox*& spin, const QString& label,
                                      int min, int max, int default_val,
                                      const QString& tooltip) {
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 4);

    auto* lbl = new QLabel(label, container);
    lbl->setObjectName("settingLabel");
    lbl->setToolTip(tooltip);
    lbl->setMinimumWidth(180);
    layout->addWidget(lbl);
    layout->addStretch();

    spin = new QSpinBox(container);
    spin->setRange(min, max);
    spin->setValue(default_val);
    spin->setToolTip(tooltip);
    layout->addWidget(spin);

    return container;
}

QWidget* SettingsPage::CreateGraphicsTab() {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget();
    content->setObjectName("tabContent");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(4);

    // Section title
    auto* title = new QLabel("Graphics Settings", content);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);

    auto* sep = new QFrame();
    sep->setObjectName("separator");
    sep->setFrameShape(QFrame::HLine);
    layout->addWidget(sep);
    layout->addSpacing(8);

    // Resolution
    layout->addWidget(CreateDropdownRow(
        m_resolution, "Screen Resolution",
        {"1280x720", "1920x1080", "2560x1440", "3840x2160"},
        "Internal render resolution. Higher values require more GPU power."));

    // Present mode
    layout->addWidget(CreateDropdownRow(
        m_present_mode, "Present Mode",
        {"FIFO (V-Sync)", "Mailbox (Adaptive V-Sync)", "Immediate (No V-Sync)"},
        "Controls how frames are presented to the screen."));

    // Shader optimization
    layout->addWidget(CreateDropdownRow(
        m_shader_opt, "Shader Optimization",
        {"None", "Size (smaller SPIR-V)", "Performance (faster runtime)"},
        "SPIR-V optimization level for compiled shaders."));

    layout->addSpacing(12);

    // Debug toggles
    auto* debug_title = new QLabel("Debug & Validation", content);
    debug_title->setObjectName("sectionTitle");
    layout->addWidget(debug_title);
    layout->addSpacing(4);

    layout->addWidget(CreateToggleRow(
        m_vulkan_validation, "Enable Vulkan Validation Layers",
        "Enables Vulkan validation layers. Very slow — for debugging only."));
    layout->addWidget(CreateToggleRow(
        m_shader_validation, "Validate SPIR-V Shaders",
        "Validates compiled SPIR-V with spirv-val. Reports shader errors at compile time."));
    layout->addWidget(CreateToggleRow(
        m_command_buffer_dump, "Dump Command Buffers",
        "Records GPU command buffers to _Buffers/ for offline analysis."));
    layout->addWidget(CreateToggleRow(
        m_pm4_dump, "Dump PM4 Command Stream (Kyty-039)",
        "Records every PM4 packet to _Pm4Dump.txt for debugging GPU hangs. Use --dump-pm4 to set a custom path."));

    // Shader log direction
    layout->addWidget(CreateDropdownRow(
        m_shader_log_dir, "Shader Log Output",
        {"Silent", "Console", "File"},
        "Where to output shader compilation logs."));

    layout->addSpacing(12);

    // Compatibility toggles
    auto* compat_title = new QLabel("Compatibility", content);
    compat_title->setObjectName("sectionTitle");
    layout->addWidget(compat_title);
    layout->addSpacing(4);

    layout->addWidget(CreateToggleRow(
        m_vulkan_relax, "Relax Vulkan Requirements",
        "Makes missing Vulkan extensions (fragmentShaderBarycentric, color_write_enable, depth_clip) optional instead of failing. For older GPUs (Pascal GTX 10-series, Intel iGPUs). May cause visual glitches."));
    layout->addWidget(CreateToggleRow(
        m_readback_linear, "Readback Linear Images",
        "Downloads linear images back to CPU for inspection. Slow — for debugging."));
    layout->addWidget(CreateToggleRow(
        m_tessellation, "Enable Tessellation",
        "Draws tessellation patches. Required for games that use tessellation."));

    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget* SettingsPage::CreateAudioTab() {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget();
    content->setObjectName("tabContent");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(4);

    auto* title = new QLabel("Audio Settings", content);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);

    auto* sep = new QFrame();
    sep->setObjectName("separator");
    sep->setFrameShape(QFrame::HLine);
    layout->addWidget(sep);
    layout->addSpacing(8);

    // Audio backend
    layout->addWidget(CreateDropdownRow(
        m_audio_backend, "Audio Backend",
        {"SDL (default)", "Cubeb (lower latency)"},
        "Audio output backend. Cubeb has lower latency on Linux/PipeWire."));

    // Master volume
    auto* vol_row = CreateSliderRow(
        m_volume, m_volume_label, "Master Volume", 0, 100, 100,
        "Master audio volume (0-100%).");
    layout->addWidget(vol_row);

    // Audio input (microphone)
    layout->addWidget(CreateDropdownRow(
        m_audio_input, "Microphone Input",
        {"None (silence)", "Default Device"},
        "Microphone input for games that use voice chat."));

    // Background music
    layout->addWidget(CreateToggleRow(
        m_bg_music, "Launcher Background Music",
        "Plays background music in the launcher UI."));

    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget* SettingsPage::CreateAdvancedTab() {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget();
    content->setObjectName("tabContent");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(4);

    auto* title = new QLabel("Advanced Settings", content);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);

    auto* sep = new QFrame();
    sep->setObjectName("separator");
    sep->setFrameShape(QFrame::HLine);
    layout->addWidget(sep);
    layout->addSpacing(8);

    // VBlank frequency
    layout->addWidget(CreateSliderRow(
        m_vblank_freq, m_vblank_label, "VBlank Frequency (Hz)", 30, 120, 60,
        "Virtual display refresh rate. Default: 60. Higher values unlock higher framerates (120Hz = 120fps cap)."));

    // Console language
    auto* lang_row = new QWidget();
    auto* lang_layout = new QHBoxLayout(lang_row);
    lang_layout->setContentsMargins(0, 4, 0, 4);
    auto* lang_label = new QLabel("Console Language", lang_row);
    lang_label->setObjectName("settingLabel");
    lang_label->setMinimumWidth(180);
    lang_layout->addWidget(lang_label);
    lang_layout->addStretch();
    m_console_language = new QSpinBox(lang_row);
    m_console_language->setRange(0, 29);
    m_console_language->setValue(1);
    m_console_language->setToolTip("Console language ID (0=Japanese, 1=English US, ...)");
    lang_layout->addWidget(m_console_language);
    layout->addWidget(lang_row);

    // GPU index
    layout->addWidget(CreateSpinRow(
        m_gpu_index, "GPU Device Index", -1, 15, -1,
        "Vulkan physical device index. -1 = auto-select. Use for multi-GPU systems."));

    // Storage bandwidth
    layout->addWidget(CreateSliderRow(
        m_storage_bw, m_storage_label, "Storage I/O Bandwidth (MB/s)", 0, 5500, 0,
        "Storage I/O throttle. 0=native speed, 5500=PS5 SSD speed. (Kyty-009)"));

    // Memory compression
    layout->addWidget(CreateDropdownRow(
        m_memory_compression, "Memory Compression",
        {"Off", "Fast", "Balanced", "Max"},
        "Compress cold memory pages to reduce RAM usage. (Kyty-010) For hosts with <16GB RAM."));

    layout->addSpacing(12);

    // Profiling toggles
    auto* prof_title = new QLabel("Profiling & Debugging", content);
    prof_title->setObjectName("sectionTitle");
    layout->addWidget(prof_title);
    layout->addSpacing(4);

    layout->addWidget(CreateToggleRow(
        m_profiler, "Enable Tracy Profiler",
        "Enables real-time profiling with Tracy. Connect with Tracy Profiler GUI to see per-frame timing."));
    layout->addWidget(CreateToggleRow(
        m_renderdoc, "Enable RenderDoc Capture",
        "Allows capturing frames with RenderDoc for GPU debugging."));
    layout->addWidget(CreateToggleRow(
        m_amd_cpu, "Apply AMD CPU Instruction Patches",
        "Patches x86 instructions that AMD CPUs handle differently (e.g. VRSQRTPS)."));
#if defined(_WIN32)
    layout->addWidget(CreateToggleRow(
        m_red_zone, "Enable Red Zone Protection (Windows)",
        "Protects Windows guest red zones when emulating certain x86 instructions."));
#endif

    // Printf/log direction
    layout->addWidget(CreateDropdownRow(
        m_printf_dir, "Log Output Direction",
        {"Silent", "Console", "File"},
        "Where to output game printf/log messages."));
    layout->addWidget(CreateDropdownRow(
        m_printf_output, "Log File Path",
        {"_kyty.txt", "_kyty_debug.txt", "_kyty_trace.txt"},
        "Output file for game log messages."));

    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget* SettingsPage::CreateHacksTab() {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget();
    content->setObjectName("tabContent");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(4);

    auto* title = new QLabel("Per-Game Hacks", content);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);

    auto* desc = new QLabel(
        "These flags override emulator behavior for specific games. "
        "They are automatically applied based on the game's CUSA ID "
        "(see data/game_hacks.json). Enable manually to test workarounds.", content);
    desc->setObjectName("settingDesc");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto* sep = new QFrame();
    sep->setObjectName("separator");
    sep->setFrameShape(QFrame::HLine);
    layout->addWidget(sep);
    layout->addSpacing(8);

    // Render hacks
    auto* render_title = new QLabel("Render Hacks", content);
    render_title->setObjectName("sectionTitle");
    layout->addWidget(render_title);
    layout->addSpacing(4);

    layout->addWidget(CreateToggleRow(
        m_hack_depth_disable, "DepthDisable — Disable depth buffer",
        "Workaround for depth-texture crashes. Will cause visual artifacts."));
    layout->addWidget(CreateToggleRow(
        m_hack_async_compute, "DisableAsyncCompute — Force synchronous compute",
        "Kyty-006 workaround for depth-texture revert cluster. Needed by Sifu, Returnal, Spider-Man."));
    layout->addWidget(CreateToggleRow(
        m_hack_depth_range, "ForceDepthRangeRestricted — Clamp depth to [0,1]",
        "Safe path that doesn't require VK_EXT_depth_range_unrestricted. Needed by Sifu, Returnal."));
    layout->addWidget(CreateToggleRow(
        m_hack_srgb, "DisableSRGB — Disable sRGB display",
        "Disables sRGB color space conversion."));
    layout->addWidget(CreateToggleRow(
        m_hack_tiling, "SkipUnknownTiling — Skip unknown texture tiling",
        "Skips textures with unknown tile modes instead of crashing."));
    layout->addWidget(CreateToggleRow(
        m_hack_fmv, "DisableFMV — Disable full-motion video",
        "Skips FMV playback. For games that crash during cutscenes."));

    layout->addSpacing(12);

    // Shader hacks
    auto* shader_title = new QLabel("Shader Hacks", content);
    shader_title->setObjectName("sectionTitle");
    layout->addWidget(shader_title);
    layout->addSpacing(4);

    layout->addWidget(CreateToggleRow(
        m_hack_shader_assert, "SkipShaderAssert — Skip shader ASSERT failures",
        "Kyty-001 workaround for missing opcodes. Lets unverified games boot past ASSERT with artifacts."));
    layout->addWidget(CreateToggleRow(
        m_hack_compute_disable, "ComputeDisable — Disable compute shaders",
        "Disables all compute shaders. For debugging only."));

    layout->addSpacing(12);

    // Game-specific hacks
    auto* game_title = new QLabel("Game-Specific Hacks", content);
    game_title->setObjectName("sectionTitle");
    layout->addWidget(game_title);
    layout->addSpacing(4);

    layout->addWidget(CreateToggleRow(
        m_hack_bloodborne_audio, "BloodborneAudioFix — Prevent audio loss",
        "Fixes audio loss in Bloodborne after a few minutes. (from rainmaker's shadPS4 fix)"));
    layout->addWidget(CreateToggleRow(
        m_hack_pm4_type0, "Pm4Type0Fix — PM4 Type 0 packet handling",
        "Handles PM4 Type 0 packets differently for Bloodborne."));
    layout->addWidget(CreateToggleRow(
        m_hack_ps4_pro, "ForcePs4ProMode — Force PS4 Pro mode",
        "Enables PS4 Pro enhanced features (higher resolution, better framerate)."));

    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

void SettingsPage::LoadFromConfig(const Configuration& config) {
    // Graphics
    m_resolution->setCurrentIndex(static_cast<int>(config.screen_resolution));
    m_present_mode->setCurrentIndex(static_cast<int>(config.present_mode));
    m_shader_opt->setCurrentIndex(static_cast<int>(config.shader_optimization_type));
    m_vulkan_validation->setChecked(config.vulkan_validation_enabled);
    m_shader_validation->setChecked(config.shader_validation_enabled);
    m_shader_log_dir->setCurrentIndex(static_cast<int>(config.shader_log_direction));
    m_command_buffer_dump->setChecked(config.command_buffer_dump_enabled);
    m_pm4_dump->setChecked(false); // pm4_dump_enabled is in Config::, not Configuration
    m_vulkan_relax->setChecked(config.vulkan_relax_requirements);
    m_readback_linear->setChecked(config.readback_linear_images);
    m_tessellation->setChecked(config.tessellation_enabled);

    // Advanced
    m_vblank_freq->setValue(config.vblank_frequency);
    m_console_language->setValue(config.console_language);
    m_gpu_index->setValue(config.gpu_index);
    m_profiler->setChecked(config.profiler_enabled);
    m_renderdoc->setChecked(config.renderdoc_enabled);
    m_amd_cpu->setChecked(config.amd_cpu_enabled);
#if defined(_WIN32)
    m_red_zone->setChecked(config.red_zone_protection_enabled);
#endif
    m_printf_dir->setCurrentIndex(static_cast<int>(config.printf_direction));

    // Hacks — these are read from HackFeatures, not Configuration
    // The checkboxes reflect the current game's hack state (if any)
    // For now, leave unchecked — they'll be populated when a game is selected
}

void SettingsPage::SaveToConfig(Configuration& config) const {
    // Graphics
    config.screen_resolution = static_cast<Configuration::Resolution>(m_resolution->currentIndex());
    config.present_mode = static_cast<Configuration::PresentMode>(m_present_mode->currentIndex());
    config.shader_optimization_type =
        static_cast<Configuration::ShaderOptimizationType>(m_shader_opt->currentIndex());
    config.vulkan_validation_enabled = m_vulkan_validation->isChecked();
    config.shader_validation_enabled = m_shader_validation->isChecked();
    config.shader_log_direction =
        static_cast<Configuration::LogDirection>(m_shader_log_dir->currentIndex());
    config.command_buffer_dump_enabled = m_command_buffer_dump->isChecked();
    config.vulkan_relax_requirements = m_vulkan_relax->isChecked();
    config.readback_linear_images = m_readback_linear->isChecked();
    config.tessellation_enabled = m_tessellation->isChecked();

    // Advanced
    config.vblank_frequency = m_vblank_freq->value();
    config.console_language = m_console_language->value();
    config.gpu_index = m_gpu_index->value();
    config.profiler_enabled = m_profiler->isChecked();
    config.renderdoc_enabled = m_renderdoc->isChecked();
    config.amd_cpu_enabled = m_amd_cpu->isChecked();
#if defined(_WIN32)
    config.red_zone_protection_enabled = m_red_zone->isChecked();
#endif
    config.printf_direction =
        static_cast<Configuration::LogDirection>(m_printf_dir->currentIndex());

    // emit outside const method — caller should connect to settingsChanged
    // and read values via LoadFromConfig/SaveToConfig pattern
}
