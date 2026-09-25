#ifndef LAUNCHER_INCLUDE_SETTINGS_PAGE_H_
#define LAUNCHER_INCLUDE_SETTINGS_PAGE_H_

#include "configuration.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QWidget>

// Kyty-UI: Redesigned settings page with real controls for every config field.
// Replaces the old "just show the .ini path" approach with a proper tabbed UI
// matching the Design A mockup (Steam-like dark theme).
//
// 5 tabs: Graphics, Audio, Input, Advanced, Hacks
// Every control is wired to Configuration fields — no .ini editing needed.

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);
    ~SettingsPage() override = default;

    // Load values from a Configuration into the UI controls
    void LoadFromConfig(const Configuration& config);

    // Write UI control values back into a Configuration
    void SaveToConfig(Configuration& config) const;

signals:
    void settingsChanged();

private:
    void SetupUi();
    QWidget* CreateGraphicsTab();
    QWidget* CreateAudioTab();
    QWidget* CreateAdvancedTab();
    QWidget* CreateHacksTab();

    // Helper: create a labeled toggle row
    static QWidget* CreateToggleRow(QCheckBox*& checkbox, const QString& label,
                                     const QString& tooltip);
    // Helper: create a labeled dropdown row
    static QWidget* CreateDropdownRow(QComboBox*& combo, const QString& label,
                                       const QStringList& items, const QString& tooltip);
    // Helper: create a labeled slider row
    static QWidget* CreateSliderRow(QSlider*& slider, QLabel*& value_label,
                                     const QString& label, int min, int max,
                                     int default_val, const QString& tooltip);
    // Helper: create a labeled spinbox row
    static QWidget* CreateSpinRow(QSpinBox*& spin, const QString& label,
                                   int min, int max, int default_val,
                                   const QString& tooltip);

    // --- Graphics tab controls ---
    QComboBox* m_resolution = nullptr;
    QComboBox* m_present_mode = nullptr;
    QComboBox* m_shader_opt = nullptr;
    QCheckBox* m_vulkan_validation = nullptr;
    QCheckBox* m_shader_validation = nullptr;
    QComboBox* m_shader_log_dir = nullptr;
    QCheckBox* m_command_buffer_dump = nullptr;
    QCheckBox* m_pm4_dump = nullptr;
    QComboBox* m_memory_compression = nullptr;
    QCheckBox* m_vulkan_relax = nullptr;
    QCheckBox* m_readback_linear = nullptr;
    QCheckBox* m_tessellation = nullptr;

    // --- Audio tab controls ---
    QComboBox* m_audio_backend = nullptr;
    QSlider* m_volume = nullptr;
    QLabel* m_volume_label = nullptr;
    QComboBox* m_audio_input = nullptr;
    QCheckBox* m_bg_music = nullptr;

    // --- Advanced tab controls ---
    QSlider* m_vblank_freq = nullptr;
    QLabel* m_vblank_label = nullptr;
    QSpinBox* m_console_language = nullptr;
    QSpinBox* m_gpu_index = nullptr;
    QSlider* m_storage_bw = nullptr;
    QLabel* m_storage_label = nullptr;
    QCheckBox* m_profiler = nullptr;
    QCheckBox* m_renderdoc = nullptr;
    QCheckBox* m_amd_cpu = nullptr;
    QCheckBox* m_red_zone = nullptr;
    QComboBox* m_printf_dir = nullptr;
    QComboBox* m_printf_output = nullptr;

    // --- Hacks tab controls ---
    QCheckBox* m_hack_depth_disable = nullptr;
    QCheckBox* m_hack_compute_disable = nullptr;
    QCheckBox* m_hack_async_compute = nullptr;
    QCheckBox* m_hack_srgb = nullptr;
    QCheckBox* m_hack_fmv = nullptr;
    QCheckBox* m_hack_tiling = nullptr;
    QCheckBox* m_hack_depth_range = nullptr;
    QCheckBox* m_hack_shader_assert = nullptr;
    QCheckBox* m_hack_bloodborne_audio = nullptr;
    QCheckBox* m_hack_pm4_type0 = nullptr;
    QCheckBox* m_hack_ps4_pro = nullptr;

    QStackedWidget* m_tabs = nullptr;
};

#endif // LAUNCHER_INCLUDE_SETTINGS_PAGE_H_
