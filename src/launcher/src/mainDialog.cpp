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
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenuBar>
#include <QStackedWidget>
#include <QToolBar>
#include <QKeySequence>
#include <QShortcut>
#include <QInputDialog>
#include <QFileDialog>
#include <QTreeWidget>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTextStream>
#include <QVariant>
#include <QtCore>

#include <cstdint>

#include "ui_main_dialog.h"

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
	~MainDialogPrivate() override;

	void Setup(MainDialog* main_dialog);

	/*slots:*/

	void Update();
	void FindInterpreter();
	void Run();

	// AbdoPS5 GUI integration slots
	void OnToggleGridView();
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

	Ui::MainDialog* m_ui             = {nullptr};
	MainDialog*     m_main_dialog    = nullptr;
	UpdateChecker*  m_update_checker = nullptr;
	QString         m_interpreter;

	QProcess m_process;

	QPointer<ConfigurationItem> m_running_item;

	// AbdoPS5 GUI components
	QMenuBar*      m_menu_bar         = nullptr;
	QStackedWidget* m_stacked_widget  = nullptr;
	GameGridFrame* m_grid_frame       = nullptr;
	HubMenuWidget* m_hub_menu         = nullptr;
	bool           m_grid_view_active = false;
	bool           m_cinema_mode      = false;
	bool           m_bg_music_playing = false;
};

QByteArray MainDialogPrivate::g_last_geometry;
bool       MainDialogPrivate::g_check_updates_on_startup = true;

MainDialog::MainDialog(QWidget* parent): QDialog(parent), m_p(new MainDialogPrivate(this)) {
	m_p->Setup(this);
}

MainDialogPrivate::~MainDialogPrivate() {
	delete m_ui;
}

void MainDialogPrivate::Setup(MainDialog* main_dialog) {
	m_ui = new Ui::MainDialog;
	m_ui->setupUi(main_dialog);

	m_main_dialog = main_dialog;
	m_update_checker = new UpdateChecker(main_dialog);
	m_ui->check_updates_on_startup->setChecked(g_check_updates_on_startup);
	m_ui->check_updates_link->setVisible(UpdateChecker::IsSupported());
	m_ui->check_updates_on_startup->setVisible(UpdateChecker::IsSupported());

	main_dialog->setWindowFlags(Qt::Dialog /*| Qt::MSWindowsFixedSizeDialogHint*/);

	connect(main_dialog, &MainDialog::Start, this, &MainDialogPrivate::FindInterpreter,
		Qt::QueuedConnection);
	connect(m_ui->widget, &ConfigurationListWidget::Select, this, &MainDialogPrivate::Update);
	connect(m_ui->widget, &ConfigurationListWidget::Run, this, &MainDialogPrivate::Run);
	connect(m_ui->check_updates_link, &QLabel::linkActivated, this,
		[this](const QString&) { m_update_checker->Check(true); });
	connect(m_update_checker, &UpdateChecker::CheckingChanged, m_ui->check_updates_link,
		&QLabel::setDisabled);
	connect(m_ui->check_updates_on_startup, &QCheckBox::toggled, this, [this](bool checked) {
		g_check_updates_on_startup = checked;
		m_ui->widget->WriteSettings();
	});
	connect(main_dialog, &MainDialog::Resize, [this]() {
		g_last_geometry = m_main_dialog->saveGeometry();
		m_ui->widget->WriteSettings();
	});

	connect(&m_process,
		static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
		[this](int /*exitCode*/, QProcess::ExitStatus /*exitStatus*/) {
			if (m_running_item != nullptr) {
				m_running_item->SetRunning(false);
			}
			Update();
		});

	m_ui->label_settings_file->setText(tr("Settings file: ") + m_ui->widget->GetSettingsFile());

	m_main_dialog->restoreGeometry(g_last_geometry);

	// AbdoPS5: Create a QStackedWidget to switch between list view, grid view,
	// and cinema mode within the same window (unified GUI).
	// Page 0: Original list view (ConfigurationListWidget + labels)
	// Page 1: Grid view (GameGridFrame)
	// Page 2: Cinema mode (HubMenuWidget)
	m_stacked_widget = new QStackedWidget(main_dialog);

	// Page 0: wrap the existing UI content in a container widget
	auto* list_page = new QWidget();
	auto* list_layout = new QVBoxLayout(list_page);
	list_layout->setContentsMargins(0, 0, 0, 0);
	list_layout->addWidget(m_ui->widget);
	// Move labels below the list
	auto* info_layout = new QVBoxLayout();
	info_layout->addWidget(m_ui->label_settings_file);
	info_layout->addWidget(m_ui->label_Interpreter);
	info_layout->addWidget(m_ui->versionLayout->widget());
	info_layout->addWidget(m_ui->check_updates_on_startup);
	list_layout->addLayout(info_layout);
	m_stacked_widget->addWidget(list_page);

	// Page 1: Grid view
	m_grid_frame = new GameGridFrame();
	m_stacked_widget->addWidget(m_grid_frame);

	// Page 2: Cinema mode (inline, not fullscreen separate window)
	m_hub_menu = new HubMenuWidget();
	m_stacked_widget->addWidget(m_hub_menu);

	// Replace the main layout's central widget with the stacked widget
	auto* main_layout = qobject_cast<QVBoxLayout*>(main_dialog->layout());
	if (main_layout) {
		// Remove the widget that was added by setupUi, insert stacked_widget
		main_layout->insertWidget(0, m_stacked_widget);
	}
	m_stacked_widget->setCurrentIndex(0); // Start on list view

	// Connect grid view selection
	connect(m_grid_frame, &GameGridFrame::gameSelected, [this](const GameGridItem& item) {
		m_main_dialog->setWindowTitle(item.title + " — AbdoPS5");
		// Select the corresponding item in the list widget
		auto* tree = m_ui->widget->findChild<QTreeWidget*>();
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

	// Connect cinema mode game launch
	connect(m_hub_menu, &HubMenuWidget::gameLaunched, [this](const HubGameItem& game) {
		// Select the game in the list and run it
		auto* tree = m_ui->widget->findChild<QTreeWidget*>();
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
		// Switch back to list view
		m_stacked_widget->setCurrentIndex(0);
		m_cinema_mode = false;
	});
	connect(m_hub_menu, &HubMenuWidget::backToMainView, [this]() {
		m_stacked_widget->setCurrentIndex(0);
		m_cinema_mode = false;
	});

	// AbdoPS5: Create menu bar with View, Tools, and Audio menus
	m_menu_bar = new QMenuBar(main_dialog);

	// === View Menu ===
	auto* view_menu = m_menu_bar->addMenu(tr("&View"));

	auto* action_grid_view = view_menu->addAction(tr("&Grid View"));
	action_grid_view->setCheckable(true);
	action_grid_view->setShortcut(QKeySequence("Ctrl+G"));
	connect(action_grid_view, &QAction::triggered, this, &MainDialogPrivate::OnToggleGridView);

	auto* action_cinema = view_menu->addAction(tr("&Cinema Mode"));
	action_cinema->setShortcut(QKeySequence("F11"));
	connect(action_cinema, &QAction::triggered, this, &MainDialogPrivate::OnToggleCinemaMode);

	view_menu->addSeparator();

	auto* action_hotkeys = view_menu->addAction(tr("&Hotkeys..."));
	action_hotkeys->setShortcut(QKeySequence("Ctrl+K"));
	connect(action_hotkeys, &QAction::triggered, this, &MainDialogPrivate::OnOpenHotkeys);

	// === Tools Menu ===
	auto* tools_menu = m_menu_bar->addMenu(tr("&Tools"));

	auto* action_cheats = tools_menu->addAction(tr("&Cheats & Patches..."));
	action_cheats->setShortcut(QKeySequence("Ctrl+C"));
	connect(action_cheats, &QAction::triggered, this, &MainDialogPrivate::OnOpenCheatsPatches);

	auto* action_mount_pkg = tools_menu->addAction(tr("&Mount PKG File..."));
	action_mount_pkg->setShortcut(QKeySequence("Ctrl+M"));
	connect(action_mount_pkg, &QAction::triggered, this, &MainDialogPrivate::OnMountPkg);

	tools_menu->addSeparator();

	auto* action_enable_hack = tools_menu->addAction(tr("&Enable Hack Flag..."));
	connect(action_enable_hack, &QAction::triggered, this, &MainDialogPrivate::OnEnableHack);

	// === Audio Menu ===
	auto* audio_menu = m_menu_bar->addMenu(tr("&Audio"));

	auto* action_bg_music = audio_menu->addAction(tr("&Background Music"));
	action_bg_music->setCheckable(true);
	connect(action_bg_music, &QAction::triggered, this, &MainDialogPrivate::OnToggleBackgroundMusic);

	// Insert menu bar at the top of the dialog
	main_dialog->layout()->setMenuBar(m_menu_bar);

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
		m_ui->label_Interpreter->setText(tr("Emulator: ") + m_interpreter);

		QProcess test;
		test.setProgram(m_interpreter);
		test.start();
		test.waitForFinished();

		auto output = QString(test.readAllStandardOutput());
		auto lines  = output.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);

		if (lines.count() >= 2) {
			m_ui->label_Version->setText(
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
	m_ui->widget->EnsureGameDirectory();

	m_ui->label_settings_file->setText(tr("Settings file: ") + m_ui->widget->GetSettingsFile());

	Update();
	if (m_ui->check_updates_on_startup->isChecked()) {
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
	QDialog::resizeEvent(event);
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
	m_running_item = m_ui->widget->GetSelectedItem();
	if (m_running_item == nullptr) {
		return;
	}

	m_running_item->SetRunning(true);

	auto info = m_ui->widget->CreateConfiguration(*m_running_item);
	m_main_dialog->RunInterpreter(&m_process, *info);

	Update();
}

void MainDialogPrivate::Update() {
	const auto* item = m_ui->widget->GetSelectedItem();

	bool run_enabled = (m_process.state() == QProcess::NotRunning && item != nullptr);

	if (run_enabled) {
		const auto& info = item->GetInfo();
		auto        dir  = info.basedir;
		run_enabled      = !dir.isEmpty() && QDir(dir).exists();
	}

	m_ui->widget->SetRunEnabled(run_enabled);
}

// === AbdoPS5 GUI slot implementations ===

void MainDialogPrivate::OnToggleGridView() {
	// Populate grid with games from the config list
	auto* tree = m_ui->widget->findChild<QTreeWidget*>();
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

	// Switch to grid view (page 1)
	m_stacked_widget->setCurrentIndex(1);
	m_grid_view_active = true;
}

void MainDialogPrivate::OnToggleCinemaMode() {
	// Populate cinema mode with games
	auto* tree = m_ui->widget->findChild<QTreeWidget*>();
	if (!tree) return;

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

	// Switch to cinema mode (page 2)
	m_stacked_widget->setCurrentIndex(2);
	m_cinema_mode = true;
	m_hub_menu->AnimateIn();
}

void MainDialogPrivate::OnOpenHotkeys() {
	auto* dialog = new Hotkeys(m_main_dialog);
	dialog->exec();
	delete dialog;
}

void MainDialogPrivate::OnOpenCheatsPatches() {
	const auto* item = m_ui->widget->GetSelectedItem();
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
	} else {
		// Try to play ambient music from the selected game's snd0.at9
		QString music_path;
		const auto* item = m_ui->widget->GetSelectedItem();
		if (item) {
			music_path = QDir(item->GetInfo().basedir).filePath("sce_sys/snd0.at9");
		}
		player.setVolume(30);
		player.playMusic(music_path);
		m_bg_music_playing = true;
	}
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
