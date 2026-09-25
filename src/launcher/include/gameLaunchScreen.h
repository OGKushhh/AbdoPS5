#ifndef LAUNCHER_INCLUDE_GAME_LAUNCH_SCREEN_H_
#define LAUNCHER_INCLUDE_GAME_LAUNCH_SCREEN_H_

#include "game_grid_frame.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

// Kyty-UI: Game launch screen overlay (Design A).
// When a game is selected in the grid, this screen appears showing:
// - Full-bleed backdrop art (game icon, scaled + darkened)
// - Game title (large, bold)
// - Title ID + compatibility status
// - Big "Play" button
// - Quick links to Settings / Patches for this game

class GameLaunchScreen : public QWidget {
    Q_OBJECT

signals:
    void playRequested(const GameGridItem& item);
    void settingsRequested();
    void backRequested();

public:
    explicit GameLaunchScreen(QWidget* parent = nullptr);
    ~GameLaunchScreen() override = default;

    void SetGame(const GameGridItem& item);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void SetupUi();
    void UpdateBackdrop();

    QLabel*    m_backdrop       = nullptr;
    QLabel*    m_title          = nullptr;
    QLabel*    m_subtitle       = nullptr;
    QLabel*    m_compat_badge   = nullptr;
    QPushButton* m_play_btn    = nullptr;
    QPushButton* m_settings_btn = nullptr;
    QPushButton* m_back_btn    = nullptr;

    GameGridItem m_current_game;
};

#endif // LAUNCHER_INCLUDE_GAME_LAUNCH_SCREEN_H_
