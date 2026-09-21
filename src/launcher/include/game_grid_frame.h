// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's game_grid_frame.h
// Adapted for KytyPS5 (PS5 PPSA title IDs, param.json, KytyPS5 config)

#pragma once

#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

// Game grid view — displays games as a grid of cover art thumbnails.
// Alternative to the tree list view. Netflix-style browsing experience.
//
// Ported from the shadPS4 Shadlix fork, adapted for PS5:
// - Uses PPSA title IDs (vs PS4's CUSA)
// - Reads param.json (vs PS4's param.sfo)
// - Integrates with KytyPS5's launcher config

struct GameGridItem {
        QString title_id;     // e.g., "PPSA01491"
        QString title;        // Game title
        QString icon_path;    // Path to icon0.png
        QString app_path;     // Path to game directory
        QString compatibility; // e.g., "InGame", "DoesntBoot"
};

class GameGridFrame : public QTableWidget {
        Q_OBJECT

Q_SIGNALS:
        void GameGridFrameClosed();
        void gameSelected(const GameGridItem& item);

public:
        explicit GameGridFrame(QWidget* parent = nullptr);
        ~GameGridFrame() override;

        void PopulateGames(const QVector<GameGridItem>& games);
        void SetGridBackgroundImage(int row, int column);
        void RefreshGridBackgroundImage();

        [[nodiscard]] int GetIconSize() const { return m_icon_size; }
        void SetIconSize(int size) { m_icon_size = size; RefreshGrid(); }

protected:
        void keyPressEvent(QKeyEvent* event) override;
        void closeEvent(QCloseEvent* event) override;

private:
        void RefreshGrid();
        void CreateGridItem(int row, int col, const GameGridItem& item);

        int m_icon_size       = 200;
        int m_columns         = 4;
        QVector<GameGridItem> m_games;
        QString m_current_background;

private Q_SLOTS:
        void onCellClicked(int row, int column);
        void onCellDoubleClicked(int row, int column);
};
