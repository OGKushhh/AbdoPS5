// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's hotkeys.cpp
// Adapted for KytyPS5 (no IPC dependency, simplified recording)

#include "hotkeys.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#ifdef _WIN32
#define LCTRL_KEY 29
#define LALT_KEY 56
#define LSHIFT_KEY 42
#else
#define LCTRL_KEY 37
#define LALT_KEY 64
#define LSHIFT_KEY 50
#endif

Hotkeys::Hotkeys(QWidget* parent) : QDialog(parent) {
	setWindowTitle("Hotkeys Configuration");
	setModal(true);
	createDefaultBindings();
	loadBindings();
	setupUI();
}

Hotkeys::~Hotkeys() = default;

void Hotkeys::createDefaultBindings() {
	m_bindings["fullscreen"]    = {"fullscreen",    "Toggle fullscreen",       0, LCTRL_KEY, "None"};
	m_bindings["pause"]        = {"pause",         "Toggle pause",            0, 0,         "None"};
	m_bindings["restart"]      = {"restart",       "Restart game",            0, 0,         "None"};
	m_bindings["stop"]         = {"stop",          "Stop game",                0, 0,         "None"};
	m_bindings["screenshot"]   = {"screenshot",    "Take screenshot",         0, 0,         "None"};
	m_bindings["fps_counter"]  = {"fps_counter",   "Toggle FPS counter",      0, 0,         "None"};
	m_bindings["debug_overlay"]= {"debug_overlay", "Toggle debug overlay",    0, 0,         "None"};
	m_bindings["save_state"]   = {"save_state",    "Save state",               0, 0,         "None"};
	m_bindings["load_state"]   = {"load_state",    "Load state",               0, 0,         "None"};
}

void Hotkeys::loadBindings() {
	// TODO: Load from config file (data/hotkeys.json)
	// For now, use defaults
}

void Hotkeys::saveBindings() {
	// TODO: Save to data/hotkeys.json
}

void Hotkeys::setupUI() {
	auto* mainLayout = new QVBoxLayout(this);
	auto* groupBox   = new QGroupBox("Hotkey Bindings", this);
	auto* gridLayout = new QGridLayout(groupBox);

	int row = 0;
	for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
		auto& binding = it.value();
		auto* label   = new QLabel(binding.description + ":", this);
		auto* button  = new QPushButton(binding.displayText, this);
		button->setProperty("hotkey_name", it.key());
		button->setMinimumWidth(150);

		connect(button, &QPushButton::clicked, this, &Hotkeys::onButtonClicked);

		gridLayout->addWidget(label, row, 0);
		gridLayout->addWidget(button, row, 1);
		m_buttons[it.key()] = button;
		row++;
	}

	mainLayout->addWidget(groupBox);

	// Buttons
	auto* buttonLayout = new QHBoxLayout();
	auto* saveButton   = new QPushButton("Save", this);
	auto* cancelButton = new QPushButton("Cancel", this);
	buttonLayout->addStretch();
	buttonLayout->addWidget(saveButton);
	buttonLayout->addWidget(cancelButton);
	mainLayout->addLayout(buttonLayout);

	connect(saveButton, &QPushButton::clicked, [this]() {
		saveBindings();
		accept();
	});
	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

	// Recording timer
	m_recordTimer = new QTimer(this);
	m_recordTimer->setSingleShot(true);
	m_recordTimer->setInterval(5000); // 5 second timeout
	connect(m_recordTimer, &QTimer::timeout, this, &Hotkeys::onRecordTimeout);
}

void Hotkeys::onButtonClicked() {
	auto* button = qobject_cast<QPushButton*>(sender());
	if (!button) return;

	m_currentlyBinding = button->property("hotkey_name").toString();
	button->setText("Press key...");
	m_recordTimer->start();
}

void Hotkeys::onRecordTimeout() {
	if (!m_currentlyBinding.isEmpty()) {
		// Reset the button text
		auto* button = m_buttons.value(m_currentlyBinding);
		if (button) {
			auto& binding = m_bindings[m_currentlyBinding];
			button->setText(binding.displayText);
		}
		m_currentlyBinding.clear();
	}
}

HotkeyBinding Hotkeys::getBinding(const QString& name) const {
	return m_bindings.value(name);
}

QString Hotkeys::matchHotkey(int key, int modifiers) const {
	for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
		const auto& binding = it.value();
		if (binding.key == key && binding.modifiers == modifiers) {
			return it.key();
		}
	}
	return {};
}

void Hotkeys::updateButtonText(QPushButton* button, const HotkeyBinding& binding) {
	if (!button) return;
	button->setText(binding.displayText);
}
