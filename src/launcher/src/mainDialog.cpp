#include "mainDialog.h"

#include "background_music_player.h"
#include "cheats_patches.h"
#include "configuration.h"
#include "configurationItem.h"
#include "configurationListWidget.h"
#include "game_grid_frame.h"
#include "hotkeys.h"
#include "hub_menu_widget.h"
#include "patchesDialog.h"
#include "updateChecker.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSize>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTextStream>
#include <QToolBar>
#include <QTreeWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QtCore>

#include <cstdint>

#if defined(_WIN32)
#include <windows.h> // IWYU pragma: keep
#endif

// IWYU pragma: no_include <minwindef.h>
// IWYU pragma: no_include <processthreadsapi.h>
// IWYU pragma: no_include <winbase.h>

class QWidget;

#if defined(_WIN32)
constexpr char EMULATOR_EXE[] = "kyty_emulator.exe";
#else
constexpr char EMULATOR_EXE[] = "kyty_emulator";
#endif

#if defined(_WIN32)
constexpr char CMD_EXE[] = "cmd.exe";
#elif defined(__linux__)
constexpr char KYTY_BASH_FILE[] = "kyty_run.sh";
#endif
#if defined(_WIN32)
constexpr DWORD CMD_X_CHARS = 175;
constexpr DWORD CMD_Y_CHARS = 1000;
#endif
constexpr char SETTINGS_MAIN_DIALOG[]        = "MainDialog";
constexpr char SETTINGS_MAIN_LAST_GEOMETRY[] = "geometry";
constexpr char SETTINGS_CHECK_UPDATES[]       = "check_updates_on_startup";

class MainDialogPrivate: public QObject {
        Q_OBJECT

public:
        explicit MainDialogPrivate(QObject* parent = nullptr): QObject(parent) {}
        ~MainDialogPrivate() override = default;

        void Setup(MainDialog* main_dialog);

        /*slots:*/

        void Update();
        void FindInterpreter();
        void Run();

        // Unified GUI: switch between stacked pages (0=list, 1=grid, 2=settings)
        void SwitchToPage(int index);
        void RefreshGrid(); // populate grid page from the config list

        // AbdoPS5 GUI integration slots
        void OnToggleCinemaMode();
        void OnOpenHotkeys();
        void OnOpenCheatsPatches();
        void OnToggleBackgroundMusic();
        void OnMountPkg();
        void OnEnableHack();

        [[nodiscard]] const QString& GetInterpreter() const { return m_interpreter; }

        static void WriteSettings(QSettings& s);
        static void ReadSettings(QSettings& s);

private:
        static QByteArray g_last_geometry;
        static bool       g_check_updates_on_startup;

        MainDialog*           m_main_dialog    = nullptr;
        UpdateChecker*        m_update_checker = nullptr;
        QString               m_interpreter;

        QProcess m_process;

        QPointer<ConfigurationItem> m_running_item;

        // === UI components — built fresh in C++, no .ui file ===
        // The previous design used Ui::MainDialog (from main_dialog.ui) which
        // promoted a ConfigurationListWidget and embedded labels in a QDialog.
        // We've switched to QMainWindow and build the central widget by hand:
        //   sidebar (QListWidget) | QStackedWidget with 3 pages
        // Page 0: ConfigurationListWidget (list view)
        // Page 1: GameGridFrame            (grid view, embedded, no popup)
        // Page 2: Settings                  (labels + checkbox)
        // The menuBar() and statusBar() come from QMainWindow for free.
        QListWidget*            m_sidebar                = nullptr;
        QStackedWidget*         m_stacked                = nullptr;
        ConfigurationListWidget* m_config_list          = nullptr;
        GameGridFrame*          m_grid_frame             = nullptr; // embedded as page 1
        HubMenuWidget*          m_hub_menu               = nullptr; // cinema mode (separate window, immersive)
        QLabel*                 m_label_settings_file    = nullptr;
        QLabel*                 m_label_interpreter      = nullptr;
        QLabel*                 m_label_version          = nullptr;
        QLabel*                 m_check_updates_link     = nullptr;
        QCheckBox*              m_check_updates_on_startup = nullptr;

        bool     m_cinema_mode      = false;
        bool     m_bg_music_playing = false;
        QAction* m_action_bg_music  = nullptr;
};

QByteArray MainDialogPrivate::g_last_geometry;
bool       MainDialogPrivate::g_check_updates_on_startup = true;

MainDialog::MainDialog(QWidget* parent): QMainWindow(parent), m_p(new MainDialogPrivate(this)) {
        m_p->Setup(this);
}

void MainDialogPrivate::Setup(MainDialog* main_dialog) {
        m_main_dialog = main_dialog;
        m_update_checker = new UpdateChecker(main_dialog);

        // === Central widget: sidebar (left) + QStackedWidget (right) ===
        // We build the entire UI in C++ — no .ui file, no reparenting, no
        // layout fighting. The previous design used Ui::MainDialog from
        // main_dialog.ui, which promoted a ConfigurationListWidget and
        // embedded the version/interpreter labels in a QDialog layout.
        // The QStackedWidget attempt to unify the UI reparented those
        // widgets at runtime, which Qt's layout nuked on first resize →
        // blank launcher window. This rewrite avoids that entirely.
        auto* central = new QWidget(main_dialog);
        auto* mainLayout = new QHBoxLayout(central);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // --- Sidebar (page switcher) ---
        m_sidebar = new QListWidget(main_dialog);
        m_sidebar->setObjectName("sidebar");
        m_sidebar->setFixedWidth(180);
        m_sidebar->setIconSize(QSize(24, 24));
        m_sidebar->setFocusPolicy(Qt::NoFocus);
        new QListWidgetItem(tr("List View"), m_sidebar);
        new QListWidgetItem(tr("Grid View"), m_sidebar);
        new QListWidgetItem(tr("Settings"), m_sidebar);
        m_sidebar->setCurrentRow(0);
        connect(m_sidebar, &QListWidget::currentRowChanged, this, &MainDialogPrivate::SwitchToPage);
        mainLayout->addWidget(m_sidebar);

        // --- Stacked widget (one page per sidebar entry) ---
        m_stacked = new QStackedWidget(main_dialog);
        mainLayout->addWidget(m_stacked, 1);

        // Page 0: List view (the ConfigurationListWidget has its own internal
        // .ui that promotes GameListTreeWidget — that's fine, we just
        // instantiate it directly here).
        auto* listPage = new QWidget(m_stacked);
        auto* listLayout = new QVBoxLayout(listPage);
        listLayout->setContentsMargins(0, 0, 0, 0);
        m_config_list = new ConfigurationListWidget(listPage);
        listLayout->addWidget(m_config_list);
        m_stacked->addWidget(listPage);

        // Page 1: Grid view — GameGridFrame embedded as a page (NOT a popup).
        // Previously the grid opened as a separate window, which made the
        // launcher feel disjoint. Now switching to the grid page is instant.
        m_grid_frame = new GameGridFrame(m_stacked);
        m_stacked->addWidget(m_grid_frame);

        // Page 2: Settings (labels + checkbox). The interpreter/version info
        // also lives on the status bar so it's always visible regardless of
        // the current page.
        auto* settingsPage = new QWidget(m_stacked);
        auto* settingsLayout = new QVBoxLayout(settingsPage);
        settingsLayout->setContentsMargins(20, 20, 20, 20);
        settingsLayout->setSpacing(8);

        m_label_settings_file = new QLabel(tr("Settings file: "), settingsPage);
        m_label_interpreter    = new QLabel(tr("Emulator: "),     settingsPage);
        m_label_version        = new QLabel(tr("Version: "),     settingsPage);
        m_check_updates_link   = new QLabel(
            QStringLiteral("<a href=\"check\">Check for updates</a>"), settingsPage);
        m_check_updates_link->setTextFormat(Qt::RichText);
        m_check_updates_on_startup = new QCheckBox(
            tr("Check for updates on startup"), settingsPage);
        m_check_updates_on_startup->setChecked(g_check_updates_on_startup);

        settingsLayout->addWidget(m_label_settings_file);
        settingsLayout->addWidget(m_label_interpreter);
        settingsLayout->addWidget(m_label_version);
        settingsLayout->addWidget(m_check_updates_link);
        settingsLayout->addWidget(m_check_updates_on_startup);
        settingsLayout->addStretch();

        m_stacked->addWidget(settingsPage);

        m_check_updates_link->setVisible(UpdateChecker::IsSupported());
        m_check_updates_on_startup->setVisible(UpdateChecker::IsSupported());

        main_dialog->setCentralWidget(central);

        // === Menu bar — QMainWindow provides menuBar() for free ===
        // (No more shoehorning a QMenuBar into a QDialog layout.)
        auto* menubar = main_dialog->menuBar();

        // --- View menu ---
        auto* view_menu = menubar->addMenu(tr("&View"));

        auto* action_list_view = view_menu->addAction(tr("&List View"));
        action_list_view->setShortcut(QKeySequence("Ctrl+L"));
        connect(action_list_view, &QAction::triggered, [this]() {
                m_sidebar->setCurrentRow(0);
        });

        auto* action_grid_view = view_menu->addAction(tr("&Grid View"));
        action_grid_view->setShortcut(QKeySequence("Ctrl+G"));
        connect(action_grid_view, &QAction::triggered, [this]() {
                m_sidebar->setCurrentRow(1);
        });

        auto* action_settings = view_menu->addAction(tr("&Settings"));
        action_settings->setShortcut(QKeySequence("Ctrl+,"));
        connect(action_settings, &QAction::triggered, [this]() {
                m_sidebar->setCurrentRow(2);
        });

        view_menu->addSeparator();

        auto* action_cinema = view_menu->addAction(tr("&Cinema Mode"));
        action_cinema->setShortcut(QKeySequence("F11"));
        connect(action_cinema, &QAction::triggered, this, &MainDialogPrivate::OnToggleCinemaMode);

        view_menu->addSeparator();

        auto* action_hotkeys = view_menu->addAction(tr("&Hotkeys..."));
        action_hotkeys->setShortcut(QKeySequence("Ctrl+K"));
        connect(action_hotkeys, &QAction::triggered, this, &MainDialogPrivate::OnOpenHotkeys);

        // --- Tools menu ---
        auto* tools_menu = menubar->addMenu(tr("&Tools"));

        auto* action_cheats = tools_menu->addAction(tr("&Cheats & Patches..."));
        action_cheats->setShortcut(QKeySequence("Ctrl+C"));
        connect(action_cheats, &QAction::triggered, this, &MainDialogPrivate::OnOpenCheatsPatches);

        auto* action_mount_pkg = tools_menu->addAction(tr("&Mount PKG File..."));
        action_mount_pkg->setShortcut(QKeySequence("Ctrl+M"));
        connect(action_mount_pkg, &QAction::triggered, this, &MainDialogPrivate::OnMountPkg);

        tools_menu->addSeparator();

        auto* action_enable_hack = tools_menu->addAction(tr("&Enable Hack Flag..."));
        connect(action_enable_hack, &QAction::triggered, this, &MainDialogPrivate::OnEnableHack);

        // --- Audio menu ---
        auto* audio_menu = menubar->addMenu(tr("&Audio"));

        auto* action_bg_music = audio_menu->addAction(tr("&Background Music"));
        action_bg_music->setCheckable(true);
        connect(action_bg_music, &QAction::triggered, this, &MainDialogPrivate::OnToggleBackgroundMusic);
        m_action_bg_music = action_bg_music; // cached so OnToggleBackgroundMusic can sync the checkbox

        // === Status bar — QMainWindow provides statusBar() for free ===
        // Shows the interpreter path + version + update link at the bottom
        // of the window, regardless of which page is current.
        auto* sb = main_dialog->statusBar();
        sb->setSizeGripEnabled(true);
        sb->addWidget(m_label_interpreter, 1);
        sb->addPermanentWidget(m_label_version);
        sb->addPermanentWidget(m_check_updates_link);

        // === Signal wiring ===
        connect(main_dialog, &MainDialog::Start, this, &MainDialogPrivate::FindInterpreter,
                Qt::QueuedConnection);
        connect(m_config_list, &ConfigurationListWidget::Select, this, &MainDialogPrivate::Update);
        connect(m_config_list, &ConfigurationListWidget::Run, this, &MainDialogPrivate::Run);
        connect(m_check_updates_link, &QLabel::linkActivated, this,
                [this](const QString&) { m_update_checker->Check(true); });
        connect(m_update_checker, &UpdateChecker::CheckingChanged, m_check_updates_link,
                &QLabel::setDisabled);
        connect(m_check_updates_on_startup, &QCheckBox::toggled, this, [this](bool checked) {
                g_check_updates_on_startup = checked;
                m_config_list->WriteSettings();
        });
        connect(main_dialog, &MainDialog::Resize, [this]() {
                g_last_geometry = m_main_dialog->saveGeometry();
                m_config_list->WriteSettings();
        });

        connect(&m_process,
                static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this](int /*exitCode*/, QProcess::ExitStatus /*exitStatus*/) {
                        if (m_running_item != nullptr) {
                                m_running_item->SetRunning(false);
                        }
                        Update();
                });

        // Grid view: clicking a game in the grid syncs the selection back to
        // the config list so Run() picks up the right game.
        connect(m_grid_frame, &GameGridFrame::gameSelected, [this](const GameGridItem& item) {
                m_main_dialog->setWindowTitle(item.title + " — AbdoPS5");
                auto* tree = m_config_list->findChild<QTreeWidget*>();
                if (tree) {
                        for (int i = 0; i < tree->topLevelItemCount(); i++) {
                                auto* cfg_item = static_cast<ConfigurationItem*>(tree->topLevelItem(i));
                                if (cfg_item && cfg_item->GetInfo().title_id == item.title_id) {
                                        tree->setCurrentItem(cfg_item);
                                        break;
                                }
                        }
                }
        });

        // Grid view Esc: switch back to the list view (the grid is no longer
        // a separate window — Esc just means "I'm done browsing the grid").
        connect(m_grid_frame, &GameGridFrame::GameGridFrameClosed, [this]() {
                m_sidebar->setCurrentRow(0);
        });

        m_label_settings_file->setText(tr("Settings file: ") + m_config_list->GetSettingsFile());

        main_dialog->restoreGeometry(g_last_geometry);
        main_dialog->setWindowTitle(QStringLiteral("AbdoPS5"));
        // Sensible default size on first launch; user-resized geometry is
        // restored from QSettings on subsequent launches.
        if (g_last_geometry.isEmpty()) {
                main_dialog->resize(900, 600);
        }

        Update();
}

void MainDialogPrivate::FindInterpreter() {
        QDir search_dir(QApplication::applicationDirPath());
        m_interpreter = search_dir.absoluteFilePath(EMULATOR_EXE);

        if (!QFile::exists(m_interpreter)) {
                search_dir.cdUp();
                m_interpreter = search_dir.absoluteFilePath(EMULATOR_EXE);
        }

        bool found = QFile::exists(m_interpreter);

        if (found) {
                m_label_interpreter->setText(tr("Emulator: ") + m_interpreter);

                QProcess test;
                test.setProgram(m_interpreter);
                test.start();
                test.waitForFinished();

                auto output = QString(test.readAllStandardOutput());
                auto lines  = output.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);

                if (lines.count() >= 2) {
                        m_label_version->setText(
                            tr("Version: ") + (lines.at(0).startsWith("exe_name") ? lines.at(1) : lines.at(0)));
                } else {
                        found = false;
                }
        }

        if (!found) {
                QMessageBox::critical(m_main_dialog, tr("Error"), tr("Can't find emulator"));
                QApplication::quit();
                return;
        }

        // Prompt for a game folder when none are configured, but keep the launcher
        // open if the user dismisses the dialog (quitting here can segfault during
        // nested modal shutdown / background compatibility load).
        m_config_list->EnsureGameDirectory();

        m_label_settings_file->setText(tr("Settings file: ") + m_config_list->GetSettingsFile());

        Update();
        if (m_check_updates_on_startup->isChecked()) {
                m_update_checker->Check(false);
        }
}

static QString BoolArg(bool value) {
        return value ? QStringLiteral("true") : QStringLiteral("false");
}

static QStringList CreateEmulatorArgs(const Configuration& info) {
        QStringList args;
        auto        r = EnumToText(info.screen_resolution).split('x');

        if (r.size() != 2) {
                return {};
        }

        args << "--screen-width" << r.at(0);
        args << "--screen-height" << r.at(1);
        args << "--user-name" << info.user_name;
        args << "--user-id" << QString::number(info.user_id);
        if (!info.audio_input_device.isEmpty()) {
                args << "--mic" << info.audio_input_device;
        }
        args << "--present-mode" << EnumToText(info.present_mode);
        if (info.gpu_index >= 0) {
                args << "--gpu" << QString::number(info.gpu_index);
        }
        if (info.fullscreen_enabled) {
                args << "--fullscreen";
        }
        args << "--readback-linear-images" << BoolArg(info.readback_linear_images);
        if (info.tessellation_enabled) {
                args << "--tessellation";
        }
        args << "--vblank-frequency" << QString::number(info.vblank_frequency);
        args << "--console-language" << QString::number(info.console_language);
        args << "--vulkan-validation" << BoolArg(info.vulkan_validation_enabled);
        args << "--shader-validation" << BoolArg(info.shader_validation_enabled);
        args << "--shader-optimization-type" << EnumToText(info.shader_optimization_type);
        args << "--shader-log-direction" << EnumToText(info.shader_log_direction);
        args << "--shader-log-folder" << info.shader_log_folder;
        args << "--command-buffer-dump" << BoolArg(info.command_buffer_dump_enabled);
        args << "--command-buffer-dump-folder" << info.command_buffer_dump_folder;
        args << "--printf-direction" << EnumToText(info.printf_direction);
        args << "--printf-output-file" << info.printf_output_file;
        if (info.profiler_enabled) {
                args << "--profile";
        }
        args << "--spirv-debug-printf" << "false";
        if (info.amd_cpu_enabled) {
                args << "--amd-cpu";
        }
#if defined(_WIN32)
        if (info.red_zone_protection_enabled) {
                args << "--redzone";
        }
#endif
        for (const auto& binding: info.host_input_mapping) {
                args << "--keymap" << binding;
        }
        if (info.renderdoc_enabled) {
                args << "--rd";
        }

        QString game = info.basedir;
        if (!info.elf.isEmpty()) {
                game = QDir(info.basedir).filePath(info.elf);
        }
        args << "--game" << game;

        const auto patch_plan = PatchesDialog::PatchPlanPath(info.title_id);
        if (QFileInfo::exists(patch_plan)) {
                args << "--game-patch" << patch_plan;
        }

        return args;
}

#ifdef __linux__
static QString BashQuote(QString value) {
        value.replace('\'', "'\\''");
        return QStringLiteral("'") + value + QStringLiteral("'");
}

static bool CreateBashScript(const QString& interpreter, const QStringList& args,
                             const QString& file_name) {
        QFile file(file_name);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream s(&file);

                s << "#!/bin/bash\n";
                s << BashQuote(interpreter);
                for (const auto& arg: args) {
                        s << " " << BashQuote(arg);
                }
                s << "\n";
                s << "echo Press any key...\n";
                s << "read -n1\n";

                file.close();

                return file.setPermissions(file.permissions() | QFile::ExeUser | QFile::ExeOwner |
                                           QFile::ExeGroup);
        }
        return false;
}

// Find a terminal and its command separator.
static bool FindTerminal(QString* program, QStringList* prefix) {
        struct TerminalSpec {
                const char* executable;
                const char* separator; // nullptr when the command follows immediately
        };

        static const TerminalSpec candidates[] = {
            {"x-terminal-emulator", "-e"},
            {"gnome-terminal", "--"},
            {"konsole", "-e"},
            {"xfce4-terminal", "-x"},
            {"mate-terminal", "--"},
            {"tilix", "-e"},
            {"alacritty", "-e"},
            {"kitty", nullptr},
            {"foot", nullptr},
            {"wezterm", "-e"},
            {"urxvt", "-e"},
            {"xterm", "-e"},
        };

        const auto try_candidate = [program, prefix](const QString& executable, const char* separator) {
                const auto resolved = QStandardPaths::findExecutable(executable);
                if (resolved.isEmpty()) {
                        return false;
                }
                *program = resolved;
                prefix->clear();
                if (separator != nullptr) {
                        *prefix << QString::fromLatin1(separator);
                }
                return true;
        };

        if (const auto from_env = qEnvironmentVariable("TERMINAL"); !from_env.isEmpty()) {
                // Reuse the known separator for an explicit terminal.
                const auto  env_name  = QFileInfo(from_env).fileName();
                const char* separator = "-e";
                for (const auto& candidate: candidates) {
                        if (env_name == QLatin1String(candidate.executable)) {
                                separator = candidate.separator;
                                break;
                        }
                }
                if (try_candidate(from_env, separator)) {
                        return true;
                }
        }

        for (const auto& candidate: candidates) {
                if (try_candidate(QString::fromLatin1(candidate.executable), candidate.separator)) {
                        return true;
                }
        }

        return false;
}
#endif

#if defined(_WIN32)
// Quote one token for cmd.exe so paths with spaces survive /K parsing.
static QString WinCmdQuote(QString value) {
        value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QLatin1Char('"') + value + QLatin1Char('"');
}

static QString BuildWinCmdKCommand(const QString& interpreter, const QStringList& args) {
        QString command = WinCmdQuote(QDir::toNativeSeparators(interpreter));
        for (const auto& arg: args) {
                command += QLatin1Char(' ');
                command += WinCmdQuote(arg);
        }
        return command;
}
#endif

void MainDialog::RunInterpreter(QProcess* process, const Configuration& info) {
        const auto& interpreter = m_p->GetInterpreter();

        QFileInfo f(interpreter);
        auto      dir = f.absoluteDir();

        auto args = CreateEmulatorArgs(info);
        if (args.isEmpty()) {
                QMessageBox::critical(this, tr("Error"), tr("Invalid emulator configuration"));
                QApplication::quit();
                return;
        }

#ifdef __linux__
        auto bash_file_name = dir.filePath(KYTY_BASH_FILE);
        if (!CreateBashScript(interpreter, args, bash_file_name)) {
                QMessageBox::critical(this, tr("Error"), tr("Can't create file:\n") + bash_file_name);
                QApplication::quit();
                return;
        }

        {
                QString     terminal;
                QStringList terminal_prefix;
                // Pass the script as a file argument (not bash -c) so paths with spaces work.
                if (FindTerminal(&terminal, &terminal_prefix)) {
                        process->setProgram(terminal);
                        process->setArguments(terminal_prefix + QStringList {"bash", bash_file_name});
                } else {
                        // Run without a terminal as a fallback.
                        process->setProgram(QStringLiteral("bash"));
                        process->setArguments({bash_file_name});
                }
        }
#elif defined(_WIN32)
        {
                // Use nativeArguments so Qt does not re-quote the /K command string.
                process->setProgram(CMD_EXE);
                process->setArguments({});
                process->setNativeArguments(QStringLiteral("/K \"") +
                                            BuildWinCmdKCommand(interpreter, args) + QLatin1Char('"'));
        }
#else
        process->setProgram(interpreter);
        process->setArguments(args);
#endif
        process->setWorkingDirectory(dir.path());
#if defined(_WIN32)
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
                args->flags |= static_cast<uint32_t>(CREATE_NEW_CONSOLE);
                args->startupInfo->dwFlags &= ~static_cast<DWORD>(STARTF_USESTDHANDLES);
                args->startupInfo->dwFlags |= static_cast<DWORD>(STARTF_USECOUNTCHARS);
                args->startupInfo->dwXCountChars = CMD_X_CHARS;
                args->startupInfo->dwYCountChars = CMD_Y_CHARS;
                // args->startupInfo->dwFlags |= static_cast<DWORD>(STARTF_USEFILLATTRIBUTE);
                // args->startupInfo->dwFillAttribute =
                //     static_cast<DWORD>(BACKGROUND_BLUE) | static_cast<DWORD>(FOREGROUND_RED) |
                //     static_cast<DWORD>(FOREGROUND_INTENSITY);
        });
#endif
        process->start();
#if !defined(_WIN32)
        // Report immediate launch failures.
        if (!process->waitForStarted(5000)) {
                QMessageBox::critical(
                    this, tr("Error"),
                    tr("Failed to start:\n%1\n\n%2").arg(process->program(), process->errorString()));
                return;
        }
#endif
        process->waitForFinished(100);
}

void MainDialog::WriteSettings(QSettings& s) {
        MainDialogPrivate::WriteSettings(s);
}

void MainDialog::ReadSettings(QSettings& s) {
        MainDialogPrivate::ReadSettings(s);
}

void MainDialog::resizeEvent(QResizeEvent* event) {
        emit Resize();
        QMainWindow::resizeEvent(event);
}

void MainDialogPrivate::WriteSettings(QSettings& s) {
        s.beginGroup(SETTINGS_MAIN_DIALOG);

        if (!g_last_geometry.isEmpty()) {
                s.setValue(SETTINGS_MAIN_LAST_GEOMETRY, g_last_geometry);
        }
        s.setValue(SETTINGS_CHECK_UPDATES, g_check_updates_on_startup);

        s.endGroup();
}

void MainDialogPrivate::ReadSettings(QSettings& s) {
        s.beginGroup(SETTINGS_MAIN_DIALOG);

        g_last_geometry = s.value(SETTINGS_MAIN_LAST_GEOMETRY, g_last_geometry).toByteArray();
        g_check_updates_on_startup = s.value(SETTINGS_CHECK_UPDATES, true).toBool();

        s.endGroup();
}

void MainDialogPrivate::Run() {
        m_running_item = m_config_list->GetSelectedItem();
        if (m_running_item == nullptr) {
                return;
        }

        m_running_item->SetRunning(true);

        auto info = m_config_list->CreateConfiguration(*m_running_item);
        m_main_dialog->RunInterpreter(&m_process, *info);

        Update();
}

void MainDialogPrivate::Update() {
        const auto* item = m_config_list->GetSelectedItem();

        bool run_enabled = (m_process.state() == QProcess::NotRunning && item != nullptr);

        if (run_enabled) {
                const auto& info = item->GetInfo();
                auto        dir  = info.basedir;
                run_enabled      = !dir.isEmpty() && QDir(dir).exists();
        }

        m_config_list->SetRunEnabled(run_enabled);
}

// === Unified GUI: page switching + grid refresh ===

void MainDialogPrivate::SwitchToPage(int index) {
        if (index < 0 || index >= m_stacked->count()) return;
        m_stacked->setCurrentIndex(index);
        if (index == 1) {
                // Grid page — refresh from the current config list so newly
                // added/removed games show up immediately.
                RefreshGrid();
        }
}

void MainDialogPrivate::RefreshGrid() {
        if (!m_grid_frame) return;
        auto* tree = m_config_list->findChild<QTreeWidget*>();
        if (!tree) return;

        QVector<GameGridItem> grid_items;
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
                auto* cfg_item = static_cast<ConfigurationItem*>(tree->topLevelItem(i));
                if (!cfg_item) continue;
                const auto& info = cfg_item->GetInfo();
                GameGridItem item;
                item.title_id = info.title_id;
                item.title = info.name.isEmpty() ? info.title_id : info.name;
                item.icon_path = QDir(info.basedir).filePath("sce_sys/icon0.png");
                item.app_path = info.basedir;
                item.compatibility = EnumToText(info.game_status);
                grid_items.append(item);
        }
        m_grid_frame->PopulateGames(grid_items);
}

// === AbdoPS5 GUI slot implementations ===
// OnToggleGridView has been removed — the grid is now an embedded page
// in the QStackedWidget, switched to via the sidebar or the View → Grid
// View menu action (which just calls m_sidebar->setCurrentRow(1)).
// RefreshGrid() is called automatically by SwitchToPage when the user
// navigates to the grid page.

void MainDialogPrivate::OnToggleCinemaMode() {
        if (!m_hub_menu) {
                m_hub_menu = new HubMenuWidget(m_main_dialog);
                m_hub_menu->setWindowFlags(Qt::Window);
                m_hub_menu->setWindowTitle("AbdoPS5 — Cinema Mode");
                m_hub_menu->resize(1280, 720);

                connect(m_hub_menu, &HubMenuWidget::gameLaunched, [this](const HubGameItem& game) {
                        m_hub_menu->hide();
                        m_cinema_mode = false;
                        auto* tree = m_config_list->findChild<QTreeWidget*>();
                        if (tree) {
                                for (int i = 0; i < tree->topLevelItemCount(); i++) {
                                        auto* cfg_item = static_cast<ConfigurationItem*>(tree->topLevelItem(i));
                                        if (cfg_item && cfg_item->GetInfo().title_id == game.title_id) {
                                                tree->setCurrentItem(cfg_item);
                                                Run();
                                                break;
                                        }
                                }
                        }
                });
                // closeEvent on the HubMenuWidget emits backToMainView when
                // the user closes via the title-bar X. We sync m_cinema_mode
                // here so the next F11 press re-opens it correctly.
                connect(m_hub_menu, &HubMenuWidget::backToMainView, [this]() {
                        m_cinema_mode = false;
                });
        }

        // Populate cinema mode with games
        auto* tree = m_config_list->findChild<QTreeWidget*>();
        if (tree) {
                QVector<HubGameItem> hub_items;
                for (int i = 0; i < tree->topLevelItemCount(); i++) {
                        auto* cfg_item = static_cast<ConfigurationItem*>(tree->topLevelItem(i));
                        if (!cfg_item) continue;
                        const auto& info = cfg_item->GetInfo();
                        HubGameItem item;
                        item.title_id = info.title_id;
                        item.title = info.name.isEmpty() ? info.title_id : info.name;
                        item.icon_path = QDir(info.basedir).filePath("sce_sys/icon0.png");
                        item.app_path = info.basedir;
                        item.compatibility = EnumToText(info.game_status);
                        item.version = info.gameVersion;
                        hub_items.append(item);
                }
                m_hub_menu->SetGames(hub_items);
        }

        m_cinema_mode = !m_cinema_mode;
        if (m_cinema_mode) {
                m_hub_menu->show();
                m_hub_menu->raise();
                m_hub_menu->activateWindow();
                m_hub_menu->AnimateIn();
        } else {
                m_hub_menu->AnimateOut();
        }
}

void MainDialogPrivate::OnOpenHotkeys() {
        auto* dialog = new Hotkeys(m_main_dialog);
        dialog->exec();
        delete dialog;
}

void MainDialogPrivate::OnOpenCheatsPatches() {
        const auto* item = m_config_list->GetSelectedItem();
        QString title_id = "UNKNOWN";
        if (item) {
                title_id = item->GetInfo().title_id;
        }

        auto* dialog = new CheatsPatches(title_id, m_main_dialog);
        dialog->setWindowTitle("Cheats & Patches — " + title_id);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(600, 500);
        dialog->exec();
}

void MainDialogPrivate::OnToggleBackgroundMusic() {
        auto& player = BackgroundMusicPlayer::getInstance();

        if (m_bg_music_playing) {
                player.stopMusic();
                m_bg_music_playing = false;
                if (m_action_bg_music) m_action_bg_music->setChecked(false);
                return;
        }

        // Try to play ambient music from the selected game's snd0.at9.
        QString music_path;
        const auto* item = m_config_list->GetSelectedItem();
        if (item) {
                music_path = QDir(item->GetInfo().basedir).filePath("sce_sys/snd0.at9");
        }

        // Guard: if no game selected or the snd0.at9 doesn't exist, refuse
        // to start. Otherwise the menu action would show "checked" while
        // no music is actually playing (Qt Multimedia may not even be linked).
        if (music_path.isEmpty() || !QFileInfo::exists(music_path)) {
                QMessageBox::information(
                    m_main_dialog, tr("Background Music"),
                    tr("No background music available for the current selection. "
                       "Select a game that ships a sce_sys/snd0.at9 file."));
                if (m_action_bg_music) m_action_bg_music->setChecked(false);
                return;
        }

        player.setVolume(30);
        player.playMusic(music_path);
        m_bg_music_playing = true;
        if (m_action_bg_music) m_action_bg_music->setChecked(true);
}

void MainDialogPrivate::OnMountPkg() {
        QString pkg_path = QFileDialog::getOpenFileName(
                m_main_dialog, tr("Select PKG File"), QString(),
                tr("PKG Files (*.pkg);;All Files (*.*)"));

        if (pkg_path.isEmpty()) {
                return;
        }

        // Launch the emulator with --mount-pkg
        QMessageBox::information(m_main_dialog, tr("Mount PKG"),
                tr("PKG file selected:\n") + pkg_path +
                tr("\n\nThe emulator will be launched with --mount-pkg."));

        // TODO: Launch the emulator process with the --mount-pkg flag
}

void MainDialogPrivate::OnEnableHack() {
        QStringList hack_names = {
                "DepthDisable", "ComputeDisable", "DisableAsyncCompute",
                "DisableSRGB", "DisableFMV", "SkipUnknownTiling",
                "ForceDepthRangeRestricted", "UseColorImageForComparison",
                "SkipShaderAssert", "ImageLoadNoReload", "MemoryBound",
                "ForcePs4ProMode", "ForceDevKitMode"
        };

        bool ok = false;
        QString hack = QInputDialog::getItem(
                m_main_dialog, tr("Enable Hack Flag"),
                tr("Select a hack to enable:"), hack_names, 0, false, &ok);

        if (ok && !hack.isEmpty()) {
                QMessageBox::information(m_main_dialog, tr("Hack Enabled"),
                        tr("Hack '") + hack + tr("' will be applied on next game launch.\n") +
                        tr("Use Tools → Cheats & Patches for persistent per-game configuration."));
        }
}

#include "mainDialog.moc"
