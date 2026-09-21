// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's cheats_patches.cpp

#include "cheats_patches.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QStandardPaths>

// All 13 hack flags from Kyty-003
static const struct {
	const char* name;
	const char* description;
	bool default_enabled;
} kHacks[] = {
	{"DepthDisable",              "Disable depth buffer (workaround for depth crashes)", false},
	{"ComputeDisable",            "Disable compute shaders (debugging)",                  false},
	{"DisableAsyncCompute",       "Force synchronous compute (depth-texture workaround)", false},
	{"DisableSRGB",               "Disable sRGB display",                                 false},
	{"DisableFMV",                "Disable full-motion video playback",                  false},
	{"SkipUnknownTiling",         "Skip unknown texture tiling types",                   false},
	{"ForceDepthRangeRestricted", "Clamp depth ranges to [0,1] (depth-texture workaround)", false},
	{"UseColorImageForComparison","Use color images for comparison textures (lossy)",     false},
	{"SkipShaderAssert",          "Skip unsupported shader opcodes instead of crashing",  false},
	{"ImageLoadNoReload",         "Never reload textures (performance hack)",           false},
	{"MemoryBound",               "Limit GPU memory allocation (iGPU workaround)",        false},
	{"ForcePs4ProMode",           "Force PS4 Pro mode",                                   false},
	{"ForceDevKitMode",           "Force DevKit mode",                                    false},
};

CheatsPatches::CheatsPatches(const QString& title_id, QWidget* parent)
    : QWidget(parent), m_title_id(title_id) {
	setupUI();
	loadHacks();
	loadPatches();
}

CheatsPatches::~CheatsPatches() = default;

void CheatsPatches::setupUI() {
	auto* mainLayout = new QVBoxLayout(this);

	m_tabs = new QTabWidget(this);

	// === Tab 1: Hacks ===
	auto* hacksScroll = new QScrollArea();
	m_hacksTab = new QWidget();
	m_hacksLayout = new QVBoxLayout(m_hacksTab);

	auto* hacksLabel = new QLabel("Per-game hack flags (Kyty-003). These toggle emulator "
				      "behavior to work around bugs. Some hacks cause visual "
				      "artifacts or performance loss.");
	m_hacksLayout->addWidget(hacksLabel);

	for (const auto& hack : kHacks) {
		auto* checkbox = new QCheckBox(QString(hack.description));
		checkbox->setChecked(hack.default_enabled);
		checkbox->setProperty("hack_name", QString(hack.name));
		m_hacksCheckboxes[QString(hack.name)] = checkbox;
		m_hacksLayout->addWidget(checkbox);
	}

	m_hacksLayout->addStretch();

	// Buttons
	auto* hacksButtonLayout = new QHBoxLayout();
	auto* applyHacksBtn = new QPushButton("Apply Hacks", this);
	auto* resetHacksBtn  = new QPushButton("Reset to Default", this);
	hacksButtonLayout->addStretch();
	hacksButtonLayout->addWidget(applyHacksBtn);
	hacksButtonLayout->addWidget(resetHacksBtn);
	m_hacksLayout->addLayout(hacksButtonLayout);

	hacksScroll->setWidget(m_hacksTab);
	hacksScroll->setWidgetResizable(true);
	m_tabs->addTab(hacksScroll, "Hacks");

	// === Tab 2: Patches (GoldHEN/ETAHen JSON) ===
	m_patchesTab = new QWidget();
	auto* patchesLayout = new QVBoxLayout(m_patchesTab);

	auto* patchesLabel = new QLabel("GoldHEN/ETAHen cheat file (JSON format). "
				       "Loaded from _Patches/<title_id>.json");
	patchesLayout->addWidget(patchesLabel);

	m_patchJsonEdit = new QTextEdit();
	m_patchJsonEdit->setFont(QFont("Consolas", 10));
	patchesLayout->addWidget(m_patchJsonEdit);

	auto* patchesButtonLayout = new QHBoxLayout();
	auto* applyPatchesBtn = new QPushButton("Apply Patches", this);
	auto* loadFileBtn     = new QPushButton("Load File...", this);
	patchesButtonLayout->addStretch();
	patchesButtonLayout->addWidget(loadFileBtn);
	patchesButtonLayout->addWidget(applyPatchesBtn);
	patchesLayout->addLayout(patchesButtonLayout);

	m_tabs->addTab(m_patchesTab, "Patches");

	mainLayout->addWidget(m_tabs);

	// Connections
	connect(applyHacksBtn, &QPushButton::clicked, this, &CheatsPatches::onApplyHacks);
	connect(resetHacksBtn, &QPushButton::clicked, this, &CheatsPatches::onResetHacks);
	connect(applyPatchesBtn, &QPushButton::clicked, this, &CheatsPatches::onApplyPatches);
}

void CheatsPatches::loadHacks() {
	// Load from data/game_hacks.json
	QFile file("data/game_hacks.json");
	if (file.open(QIODevice::ReadOnly)) {
		auto doc = QJsonDocument::fromJson(file.readAll());
		file.close();

		if (doc.isObject()) {
			auto obj = doc.object();
			if (obj.contains(m_title_id) && obj[m_title_id].isArray()) {
				auto hacks = obj[m_title_id].toArray();
				for (const auto& hack : hacks) {
					if (hack.isString()) {
						auto name = hack.toString();
						if (m_hacksCheckboxes.contains(name)) {
							m_hacksCheckboxes[name]->setChecked(true);
						}
					}
				}
			}
		}
	}
}

void CheatsPatches::saveHacks() {
	// Load existing JSON (or create new)
	QJsonObject obj;
	QFile file("data/game_hacks.json");
	if (file.open(QIODevice::ReadOnly)) {
		auto doc = QJsonDocument::fromJson(file.readAll());
		file.close();
		if (doc.isObject()) {
			obj = doc.object();
		}
	}

	// Build hacks array for this title
	QJsonArray hacksArray;
	for (auto it = m_hacksCheckboxes.begin(); it != m_hacksCheckboxes.end(); ++it) {
		if (it.value()->isChecked()) {
			hacksArray.append(it.key());
		}
	}

	// Update the title's entry
	if (!hacksArray.isEmpty()) {
		obj[m_title_id] = hacksArray;
	} else {
		obj.remove(m_title_id);
	}

	// Ensure data/ directory exists
	QDir().mkpath("data");

	// Write back
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
		file.close();
	}
}

void CheatsPatches::loadPatches() {
	// Load from _Patches/<title_id>.json
	m_patchFilePath = QString("_Patches/%1.json").arg(m_title_id);
	QFile file(m_patchFilePath);
	if (file.open(QIODevice::ReadOnly)) {
		m_patchJsonEdit->setPlainText(QString::fromUtf8(file.readAll()));
		file.close();
	} else {
		m_patchJsonEdit->setPlainText(
			"{\n"
			"  \"id\": \"" + m_title_id + "\",\n"
			"  \"version\": \"1.00\",\n"
			"  \"process\": \"eboot.bin\",\n"
			"  \"mods\": []\n"
			"}"
		);
	}
}

void CheatsPatches::savePatches() {
	QDir().mkpath("_Patches");
	QFile file(m_patchFilePath);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(m_patchJsonEdit->toPlainText().toUtf8());
		file.close();
	}
}

void CheatsPatches::onApplyHacks() {
	saveHacks();
	QMessageBox::information(this, "Hacks Applied",
		"Hack flags saved to data/game_hacks.json.\n"
		"They will be applied on next game launch.");
}

void CheatsPatches::onApplyPatches() {
	savePatches();
	QMessageBox::information(this, "Patches Saved",
		"Cheat file saved to " + m_patchFilePath + "\n"
		"It will be applied on next game launch.");
}

void CheatsPatches::onResetHacks() {
	for (const auto& hack : kHacks) {
		auto* cb = m_hacksCheckboxes.value(QString(hack.name));
		if (cb) {
			cb->setChecked(hack.default_enabled);
		}
	}
}
