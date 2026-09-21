// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's cheats_patches.h
// Adapted for AbdoPS5 — integrates with Kyty-003 HackFeatures

#pragma once

#include <QCheckBox>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

// Cheat/Patch Manager dialog.
// Provides a GUI for managing per-game hacks (from Kyty-003) and
// GoldHEN-style byte patches (already supported by KytyPS5).
//
// Two tabs:
// 1. "Hacks" — toggle the 13 GameHack flags from Kyty-003
// 2. "Patches" — manage ETAHen/GoldHEN JSON cheat files

class CheatsPatches : public QWidget {
	Q_OBJECT

public:
	explicit CheatsPatches(const QString& title_id, QWidget* parent = nullptr);
	~CheatsPatches() override;

private:
	void setupUI();
	void loadHacks();
	void saveHacks();
	void loadPatches();
	void savePatches();

	QString m_title_id;

	// Hacks tab
	QTabWidget*   m_tabs            = nullptr;
	QWidget*      m_hacksTab        = nullptr;
	QVBoxLayout*  m_hacksLayout     = nullptr;
	QMap<QString, QCheckBox*> m_hackCheckboxes;

	// Patches tab
	QWidget*      m_patchesTab      = nullptr;
	QTextEdit*    m_patchJsonEdit   = nullptr;
	QString       m_patchFilePath;

private Q_SLOTS:
	void onApplyHacks();
	void onApplyPatches();
	void onResetHacks();
};
