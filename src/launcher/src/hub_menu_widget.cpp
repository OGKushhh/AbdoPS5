// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's hub_menu_widget.cpp
// Adapted for KytyPS5 — Netflix-style "Cinema Mode"

#include "hub_menu_widget.h"

#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPixmap>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

HubMenuWidget::HubMenuWidget(QWidget* parent) : QWidget(parent) {
	SetupUI();
}

HubMenuWidget::~HubMenuWidget() = default;

void HubMenuWidget::SetupUI() {
	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// Background image (blurred game cover art)
	m_backgroundLabel = new QLabel(this);
	m_backgroundLabel->setScaledContents(true);
	m_backgroundLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	mainLayout->addWidget(m_backgroundLabel, 1);

	// Content overlay
	auto* contentWidget = new QWidget(this);
	contentWidget->setStyleSheet("background: rgba(0, 0, 0, 0.7);");
	auto* contentLayout = new QVBoxLayout(contentWidget);
	contentLayout->setContentsMargins(40, 40, 40, 40);

	// Back button
	m_backButton = new QPushButton("← Back", contentWidget);
	m_backButton->setStyleSheet(
		"QPushButton { color: white; background: transparent; border: none; "
		"font-size: 16px; padding: 8px; }"
		"QPushButton:hover { color: #4488ff; }");
	contentLayout->addWidget(m_backButton, 0, Qt::AlignLeft);

	// Game info section
	auto* infoLayout = new QHBoxLayout();

	// Left: game icon
	m_iconLabel = new QLabel(contentWidget);
	m_iconLabel->setFixedSize(m_iconSize, m_iconSize);
	m_iconLabel->setStyleSheet("background: #222; border-radius: 12px;");
	infoLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);

	// Right: title + details + buttons
	auto* detailsLayout = new QVBoxLayout();

	m_titleLabel = new QLabel("Select a game", contentWidget);
	m_titleLabel->setStyleSheet("color: white; font-size: 32px; font-weight: bold;");
	detailsLayout->addWidget(m_titleLabel);

	m_compatLabel = new QLabel("", contentWidget);
	m_compatLabel->setStyleSheet("color: #88ff88; font-size: 18px;");
	detailsLayout->addWidget(m_compatLabel);

	m_versionLabel = new QLabel("", contentWidget);
	m_versionLabel->setStyleSheet("color: #aaa; font-size: 14px;");
	detailsLayout->addWidget(m_versionLabel);

	detailsLayout->addSpacing(20);

	// Play / Settings buttons
	auto* buttonLayout = new QHBoxLayout();
	m_playButton = new QPushButton("▶ Play", contentWidget);
	m_playButton->setStyleSheet(
		"QPushButton { background: #4488ff; color: white; border: none; "
		"font-size: 20px; padding: 12px 48px; border-radius: 8px; }"
		"QPushButton:hover { background: #66aaff; }");
	m_playButton->setFixedHeight(50);

	m_settingsButton = new QPushButton("⚙ Settings", contentWidget);
	m_settingsButton->setStyleSheet(
		"QPushButton { background: #333; color: white; border: none; "
		"font-size: 16px; padding: 12px 24px; border-radius: 8px; }"
		"QPushButton:hover { background: #555; }");

	buttonLayout->addWidget(m_playButton);
	buttonLayout->addWidget(m_settingsButton);
	buttonLayout->addStretch();
	detailsLayout->addLayout(buttonLayout);

	detailsLayout->addStretch();
	infoLayout->addLayout(detailsLayout, 1);

	contentLayout->addLayout(infoLayout);

	// Game strip (horizontal scroll)
	m_gamesScroll = new QScrollArea(contentWidget);
	m_gamesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_gamesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_gamesScroll->setFixedHeight(140);
	m_gamesScroll->setWidgetResizable(true);

	m_gamesContainer = new QWidget();
	m_gamesLayout = new QHBoxLayout(m_gamesContainer);
	m_gamesLayout->setContentsMargins(10, 10, 10, 10);
	m_gamesLayout->setSpacing(10);
	m_gamesScroll->setWidget(m_gamesContainer);

	contentLayout->addWidget(m_gamesScroll);

	mainLayout->addWidget(contentWidget, 0);

	// Connections
	connect(m_backButton, &QPushButton::clicked, this, &HubMenuWidget::backToMainView);
	connect(m_playButton, &QPushButton::clicked, [this]() {
		if (m_selectedIndex >= 0 && m_selectedIndex < m_games.size()) {
			emit gameLaunched(m_games[m_selectedIndex]);
		}
	});
}

void HubMenuWidget::SetGames(const QVector<HubGameItem>& games) {
	m_games = games;
	UpdateDisplayedGames();
	if (!games.isEmpty()) {
		SelectGame(0);
	}
}

void HubMenuWidget::UpdateDisplayedGames() {
	// Clear existing
	while (m_gamesLayout->count() > 0) {
		auto* item = m_gamesLayout->takeAt(0);
		if (item->widget()) {
			delete item->widget();
		}
		delete item;
	}

	// Create a button for each game
	for (int i = 0; i < m_games.size(); i++) {
		const auto& game = m_games[i];

		auto* gameWidget = new QPushButton();
		gameWidget->setFixedSize(100, 120);
		gameWidget->setProperty("game_index", i);

		QPixmap pixmap(game.icon_path);
		if (pixmap.isNull()) {
			pixmap = QPixmap(100, 120);
			pixmap.fill(Qt::darkGray);
		} else {
			pixmap = pixmap.scaled(100, 120, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		}
		gameWidget->setIcon(QIcon(pixmap));
		gameWidget->setIconSize(QSize(100, 120));
		gameWidget->setStyleSheet(
			"QPushButton { border: 2px solid transparent; border-radius: 6px; }"
			"QPushButton:hover { border-color: #4488ff; }"
			"QPushButton:checked { border-color: #4488ff; }");

		gameWidget->setCheckable(true);

		connect(gameWidget, &QPushButton::clicked, [this, i]() {
			SelectGame(i);
		});

		m_gamesLayout->addWidget(gameWidget);
	}

	m_gamesLayout->addStretch();
}

void HubMenuWidget::SelectGame(int index) {
	if (index < 0 || index >= m_games.size()) return;

	m_selectedIndex = index;
	const auto& game = m_games[index];

	// Update title
	m_titleLabel->setText(game.title);

	// Update compatibility
	QString compatText = game.compatibility;
	QString compatColor = "#aaaaaa";
	if (compatText == "InGame") compatColor = "#88ff88";
	else if (compatText == "DoesntBoot") compatColor = "#ff8888";
	else if (compatText == "MainMenu") compatColor = "#ffaa44";
	m_compatLabel->setText(compatText);
	m_compatLabel->setStyleSheet(QString("color: %1; font-size: 18px;").arg(compatColor));

	// Update version
	m_versionLabel->setText("Version: " + game.version);

	// Update icon
	QPixmap pixmap(game.icon_path);
	if (!pixmap.isNull()) {
		pixmap = pixmap.scaled(m_iconSize, m_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		m_iconLabel->setPixmap(pixmap);
	}

	// Update background
	SetBackgroundImage(game.icon_path);

	// Update game strip selection
	for (int i = 0; i < m_gamesLayout->count() - 1; i++) {
		auto* btn = qobject_cast<QPushButton*>(m_gamesLayout->itemAt(i)->widget());
		if (btn) {
			btn->setChecked(i == index);
		}
	}

	// Scroll to the selected game
	if (m_gamesLayout->count() > index) {
		auto* widget = m_gamesLayout->itemAt(index)->widget();
		if (widget) {
			m_gamesScroll->ensureWidgetVisible(widget);
		}
	}
}

void HubMenuWidget::SetBackgroundImage(const QString& path) {
	if (path.isEmpty()) return;

	QPixmap pixmap(path);
	if (pixmap.isNull()) return;

	// Scale to fill the background
	auto scaled = pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

	// Apply a dark overlay (semi-transparent)
	QPixmap overlay(scaled.size());
	overlay.fill(Qt::transparent);
	QPainter painter(&overlay);
	painter.drawPixmap(0, 0, scaled);
	painter.fillRect(overlay.rect(), QColor(0, 0, 0, 100));
	painter.end();

	m_backgroundLabel->setPixmap(overlay);
}

void HubMenuWidget::AnimateIn() {
	auto* effect = new QGraphicsOpacityEffect(this);
	setGraphicsEffect(effect);
	auto* anim = new QPropertyAnimation(effect, "opacity", this);
	anim->setDuration(300);
	anim->setStartValue(0.0);
	anim->setEndValue(1.0);
	anim->setEasingCurve(QEasingCurve::OutCubic);
	anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void HubMenuWidget::AnimateOut() {
	auto* effect = new QGraphicsOpacityEffect(this);
	setGraphicsEffect(effect);
	auto* anim = new QPropertyAnimation(effect, "opacity", this);
	anim->setDuration(300);
	anim->setStartValue(1.0);
	anim->setEndValue(0.0);
	anim->setEasingCurve(QEasingCurve::InCubic);
	connect(anim, &QPropertyAnimation::finished, this, &HubMenuWidget::hide);
	anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void HubMenuWidget::keyPressEvent(QKeyEvent* event) {
	switch (event->key()) {
		case Qt::Key_Left:
			if (m_selectedIndex > 0) SelectGame(m_selectedIndex - 1);
			break;
		case Qt::Key_Right:
			if (m_selectedIndex < m_games.size() - 1) SelectGame(m_selectedIndex + 1);
			break;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			if (m_selectedIndex >= 0 && m_selectedIndex < m_games.size()) {
				emit gameLaunched(m_games[m_selectedIndex]);
			}
			break;
		case Qt::Key_Escape:
			emit backToMainView();
			break;
		default:
			QWidget::keyPressEvent(event);
	}
}
