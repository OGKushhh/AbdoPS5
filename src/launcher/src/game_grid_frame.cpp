// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's game_grid_frame.cpp

#include "game_grid_frame.h"

#include <QCloseEvent>
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
        // Design A: card-style game tile with rounded cover art + title overlay
        auto* widget = new QWidget();
        widget->setStyleSheet(
            "QWidget { background: transparent; }"
        );
        auto* layout = new QVBoxLayout(widget);
        layout->setAlignment(Qt::AlignCenter);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);

        // Cover art container — styled as a card with rounded corners
        auto* iconLabel = new QLabel();
        iconLabel->setFixedSize(m_icon_size, m_icon_size * 3 / 4);  // 4:3 aspect
        iconLabel->setStyleSheet(
            "QLabel {"
            "  background: #1a1a2e;"
            "  border: 2px solid #334455;"
            "  border-radius: 8px;"
            "}"
            "QLabel:hover { border-color: #1a9fff; }"
        );
        QPixmap pixmap(item.icon_path);
        if (!pixmap.isNull()) {
            pixmap = pixmap.scaled(m_icon_size - 4, (m_icon_size * 3 / 4) - 4,
                                   Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            iconLabel->setPixmap(pixmap);
            iconLabel->setScaledContents(true);
        } else {
            // Generate a colored placeholder with the game's first letter
            QPixmap placeholder(m_icon_size - 4, (m_icon_size * 3 / 4) - 4);
            // Pick a color based on the title hash
            uint32_t hash = 0;
            for (auto ch : item.title.toUtf8()) { hash = hash * 31 + ch; }
            QColor placeholder_color(
                30 + (hash % 60), 30 + ((hash >> 8) % 60), 50 + ((hash >> 16) % 60));
            placeholder.fill(placeholder_color);
            iconLabel->setPixmap(placeholder);
            iconLabel->setScaledContents(true);
        }
        iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(iconLabel, 0, Qt::AlignCenter);

        // Title — styled with Design A typography
        auto* titleLabel = new QLabel(item.title);
        titleLabel->setAlignment(Qt::AlignCenter);
        titleLabel->setWordWrap(true);
        titleLabel->setMaximumWidth(m_icon_size + 10);
        titleLabel->setStyleSheet(
            "QLabel {"
            "  color: #ccddee;"
            "  font-size: 12px;"
            "  font-weight: bold;"
            "  background: transparent;"
            "  padding: 2px;"
            "}"
        );
        layout->addWidget(titleLabel);

        // Compatibility badge — pill-shaped, colored
        if (!item.compatibility.isEmpty()) {
            auto* compatLabel = new QLabel(item.compatibility);
            compatLabel->setAlignment(Qt::AlignCenter);
            compatLabel->setFixedHeight(20);
            QString color;
            if (item.compatibility == "InGame" || item.compatibility == "Playable") {
                color = "#2ecc71";  // green
            } else if (item.compatibility == "DoesntBoot" || item.compatibility == "Crash") {
                color = "#e74c3c";  // red
            } else {
                color = "#f39c12";  // orange
            }
            compatLabel->setStyleSheet(
                QString("QLabel {"
                        "  color: white;"
                        "  background: %1;"
                        "  border-radius: 10px;"
                        "  padding: 2px 10px;"
                        "  font-size: 10px;"
                        "  font-weight: bold;"
                        "}").arg(color)
            );
            layout->addWidget(compatLabel, 0, Qt::AlignCenter);
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
                // close() will fire closeEvent, which is the single source of
                // truth for emitting GameGridFrameClosed(). Don't emit here —
                // emitting here + in closeEvent would double-fire the signal.
                close();
                return;
        }
        QTableWidget::keyPressEvent(event);
}

void GameGridFrame::closeEvent(QCloseEvent* event) {
        // Single emitter for GameGridFrameClosed(): fires whether the user
        // pressed Esc, clicked the title-bar X, or called close() directly.
        // This keeps the parent's m_grid_view_active flag and the menu
        // action's checked state in sync with actual window visibility.
        emit GameGridFrameClosed();
        QTableWidget::closeEvent(event);
}
