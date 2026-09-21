// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's hub_menu_widget.h
// Adapted for KytyPS5 — Netflix-style "Cinema Mode" big picture UI

#pragma once

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

// Hub/Cinema Mode — a Netflix-style big picture UI for browsing games.
// Displays large cover art, game titles, and compatibility badges.
// Supports keyboard/controller navigation.
//
// Ported from the shadPS4 Shadlix fork, adapted for PS5.

struct HubGameItem {
	QString title_id;
	QString title;
	QString icon_path;
	QString app_path;
	QString compatibility;
	QString version;
};

class HubMenuWidget : public QWidget {
	Q_OBJECT

Q_SIGNALS:
	void gameLaunched(const HubGameItem& game);
	void backToMainView();

public:
	explicit HubMenuWidget(QWidget* parent = nullptr);
	~HubMenuWidget() override;

	void SetGames(const QVector<HubGameItem>& games);
	void SetBackgroundImage(const QString& path);
	void AnimateIn();
	void AnimateOut();

protected:
	void keyPressEvent(QKeyEvent* event) override;

private:
	void SetupUI();
	void UpdateDisplayedGames();
	void SelectGame(int index);

	QLabel*      m_backgroundLabel   = nullptr;
	QLabel*      m_titleLabel        = nullptr;
	QLabel*      m_compatLabel       = nullptr;
	QLabel*      m_versionLabel      = nullptr;
	QLabel*      m_iconLabel         = nullptr;
	QWidget*     m_gamesContainer    = nullptr;
	QHBoxLayout* m_gamesLayout       = nullptr;
	QScrollArea* m_gamesScroll       = nullptr;

	QPushButton* m_playButton        = nullptr;
	QPushButton* m_settingsButton    = nullptr;
	QPushButton* m_backButton        = nullptr;

	QVector<HubGameItem> m_games;
	int          m_selectedIndex     = 0;
	int          m_iconSize          = 320;
};
