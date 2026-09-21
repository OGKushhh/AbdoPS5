// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's game_grid_frame.cpp

#include "game_grid_frame.h"

#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPixmap>
#include <QHeaderView>

GameGridFrame::GameGridFrame(QWidget* parent) : QTableWidget(parent) {
	setSelectionBehavior(QAbstractItemView::SelectItems);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	setFocusPolicy(Qt::StrongFocus);
	verticalHeader()->setVisible(false);
	horizontalHeader()->setVisible(false);
	horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	setShowGrid(false);
	setColumnCount(m_columns);

	connect(this, &QTableWidget::cellClicked, this, &GameGridFrame::onCellClicked);
	connect(this, &QTableWidget::cellDoubleClicked, this, &GameGridFrame::onCellDoubleClicked);
}

GameGridFrame::~GameGridFrame() = default;

void GameGridFrame::PopulateGames(const QVector<GameGridItem>& games) {
	m_games = games;
	RefreshGrid();
}

void GameGridFrame::RefreshGrid() {
	clear();
	setRowCount(0);

	const int rows = (m_games.size() + m_columns - 1) / m_columns;
	setRowCount(rows);
	setColumnCount(m_columns);

	for (int i = 0; i < m_games.size(); i++) {
		const int row = i / m_columns;
		const int col = i % m_columns;
		CreateGridItem(row, col, m_games[i]);
	}

	resizeColumnsToContents();
	resizeRowsToContents();
}

void GameGridFrame::CreateGridItem(int row, int col, const GameGridItem& item) {
	// Create a custom widget with icon + title
	auto* widget = new QWidget();
	auto* layout = new QVBoxLayout(widget);
	layout->setAlignment(Qt::AlignCenter);

	// Icon
	auto* iconLabel = new QLabel();
	QPixmap pixmap(item.icon_path);
	if (!pixmap.isNull()) {
		pixmap = pixmap.scaled(m_icon_size, m_icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		iconLabel->setPixmap(pixmap);
	} else {
		// Placeholder icon
		pixmap = QPixmap(m_icon_size, m_icon_size);
		pixmap.fill(Qt::darkGray);
		iconLabel->setPixmap(pixmap);
	}
	iconLabel->setAlignment(Qt::AlignCenter);
	layout->addWidget(iconLabel);

	// Title
	auto* titleLabel = new QLabel(item.title);
	titleLabel->setAlignment(Qt::AlignCenter);
	titleLabel->setWordWrap(true);
	titleLabel->setMaximumWidth(m_icon_size + 20);
	layout->addWidget(titleLabel);

	// Compatibility badge
	if (!item.compatibility.isEmpty()) {
		auto* compatLabel = new QLabel(item.compatibility);
		compatLabel->setAlignment(Qt::AlignCenter);
		if (item.compatibility == "InGame") {
			compatLabel->setStyleSheet("color: green; font-weight: bold;");
		} else if (item.compatibility == "DoesntBoot") {
			compatLabel->setStyleSheet("color: red; font-weight: bold;");
		} else {
			compatLabel->setStyleSheet("color: orange; font-weight: bold;");
		}
		layout->addWidget(compatLabel);
	}

	setCellWidget(row, col, widget);
}

void GameGridFrame::onCellClicked(int row, int column) {
	SetGridBackgroundImage(row, column);
	const int index = row * m_columns + column;
	if (index < m_games.size()) {
		emit gameSelected(m_games[index]);
	}
}

void GameGridFrame::onCellDoubleClicked(int row, int column) {
	const int index = row * m_columns + column;
	if (index < m_games.size()) {
		emit gameSelected(m_games[index]);
	}
}

void GameGridFrame::SetGridBackgroundImage(int row, int column) {
	const int index = row * m_columns + column;
	if (index >= m_games.size()) return;

	const auto& item = m_games[index];
	if (!item.icon_path.isEmpty() && QFileInfo::exists(item.icon_path)) {
		m_current_background = item.icon_path;
		RefreshGridBackgroundImage();
	}
}

void GameGridFrame::RefreshGridBackgroundImage() {
	if (m_current_background.isEmpty()) return;

	// Set the background to a blurred version of the icon
	QPixmap bg(m_current_background);
	if (!bg.isNull()) {
		auto scaled = bg.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		// Apply blur effect (simplified — use opacity for now)
		auto palette = this->palette();
		palette.setBrush(QPalette::Window, scaled);
		setPalette(palette);
		setAutoFillBackground(true);
	}
}

void GameGridFrame::keyPressEvent(QKeyEvent* event) {
	if (event->key() == Qt::Key_Escape) {
		emit GameGridFrameClosed();
		return;
	}
	QTableWidget::keyPressEvent(event);
}
