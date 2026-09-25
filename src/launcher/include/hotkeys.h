// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's hotkeys.h
// Adapted for KytyPS5 (removed IPC client dependency, uses SDL3 directly)

#pragma once

#include <QDialog>
#include <QMap>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVector>

// Hotkeys configuration dialog.
// Lets users remap keyboard shortcuts for emulator functions:
// - Toggle fullscreen
// - Toggle pause
// - Restart game
// - Stop game
// - Take screenshot
// - Toggle FPS counter
// - Toggle debug overlay
// - Save state
// - Load state

struct HotkeyBinding {
	QString name;
	QString description;
	int key      = 0;     // SDL scancode
	int modifiers = 0;     // Ctrl/Alt/Shift bitmask
	QString displayText;    // Human-readable (e.g. "Ctrl+F11")
};

class Hotkeys : public QDialog {
	Q_OBJECT

public:
	explicit Hotkeys(QWidget* parent = nullptr);
	~Hotkeys() override;

	// Get the current binding for a hotkey name
	[[nodiscard]] HotkeyBinding getBinding(const QString& name) const;

	// Check if a key combination matches a hotkey
	[[nodiscard]] QString matchHotkey(int key, int modifiers) const;

	// Save bindings to config file
	void saveBindings();

	// Load bindings from config file
	void loadBindings();

private:
	void setupUI();
	void createDefaultBindings();
	void updateButtonText(QPushButton* button, const HotkeyBinding& binding);

	QMap<QString, HotkeyBinding> m_bindings;
	QMap<QString, QPushButton*> m_buttons;
	QString m_currentlyBinding; // Name of the hotkey being recorded
	QTimer* m_recordTimer       = nullptr;

private Q_SLOTS:
	void onButtonClicked();
	void onRecordTimeout();
};
