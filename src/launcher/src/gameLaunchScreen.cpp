#include "gameLaunchScreen.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QVBoxLayout>

// Kyty-UI: Game launch screen — backdrop art + play button.

GameLaunchScreen::GameLaunchScreen(QWidget* parent) : QWidget(parent) {
    SetupUi();
}

void GameLaunchScreen::SetupUi() {
    setStyleSheet(
        "QWidget { background: #0a0a14; }"
        "QLabel#backdropLabel { border: none; }"
        "QLabel#gameTitle {"
        "  color: #ffffff;"
        "  font-size: 28px;"
        "  font-weight: bold;"
        "  padding: 0px 0px 4px 0px;"
        "}"
        "QLabel#gameSubtitle {"
        "  color: #8899aa;"
        "  font-size: 13px;"
        "  padding: 0px 0px 8px 0px;"
        "}"
        "QLabel#compatBadge {"
        "  color: white;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  padding: 3px 12px;"
        "  border-radius: 10px;"
        "}"
        "QPushButton#playBtn {"
        "  background: #1a9fff;"
        "  color: white;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 12px 48px;"
        "  border: none;"
        "  border-radius: 6px;"
        "}"
        "QPushButton#playBtn:hover {"
        "  background: #4db8ff;"
        "}"
        "QPushButton#playBtn:pressed {"
        "  background: #0077cc;"
        "}"
        "QPushButton#secondaryBtn {"
        "  background: #1a1a2e;"
        "  color: #8899aa;"
        "  font-size: 12px;"
        "  padding: 8px 20px;"
        "  border: 1px solid #334455;"
        "  border-radius: 4px;"
        "  margin-left: 8px;"
        "}"
        "QPushButton#secondaryBtn:hover {"
        "  background: #16213e;"
        "  color: #ccddee;"
        "  border-color: #1a9fff;"
        "}"
    );

    // Backdrop layer (full-bleed background image)
    m_backdrop = new QLabel(this);
    m_backdrop->setObjectName("backdropLabel");
    m_backdrop->setScaledContents(true);
    m_backdrop->lower();

    // Content layer (on top of backdrop, anchored to bottom-left)
    auto* content = new QWidget(this);
    content->setStyleSheet("background: transparent;");
    content->setContentsMargins(0, 0, 0, 0);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(40, 0, 40, 48);
    layout->setSpacing(0);

    // Compatibility badge
    m_compat_badge = new QLabel("Unknown");
    m_compat_badge->setObjectName("compatBadge");
    m_compat_badge->setStyleSheet("background: #f39c12;"); // orange default
    layout->addWidget(m_compat_badge);
    layout->addSpacing(8);

    // Game title
    m_title = new QLabel("Select a game");
    m_title->setObjectName("gameTitle");
    layout->addWidget(m_title);

    // Subtitle (title ID + firmware)
    m_subtitle = new QLabel("");
    m_subtitle->setObjectName("gameSubtitle");
    layout->addWidget(m_subtitle);
    layout->addSpacing(16);

    // Button row
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(0);

    m_play_btn = new QPushButton("▶  Play");
    m_play_btn->setObjectName("playBtn");
    m_play_btn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_play_btn);

    m_settings_btn = new QPushButton("Settings");
    m_settings_btn->setObjectName("secondaryBtn");
    m_settings_btn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_settings_btn);

    m_back_btn = new QPushButton("← Back");
    m_back_btn->setObjectName("secondaryBtn");
    m_back_btn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_back_btn);

    btnLayout->addStretch();
    layout->addLayout(btnLayout);
    layout->addStretch();

    // Position content at bottom of the screen
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addStretch();
    mainLayout->addWidget(content);

    // Wire buttons
    connect(m_play_btn, &QPushButton::clicked, [this]() {
        emit playRequested(m_current_game);
    });
    connect(m_settings_btn, &QPushButton::clicked, [this]() {
        emit settingsRequested();
    });
    connect(m_back_btn, &QPushButton::clicked, [this]() {
        emit backRequested();
    });
}

void GameLaunchScreen::SetGame(const GameGridItem& item) {
    m_current_game = item;

    m_title->setText(item.title);
    m_subtitle->setText(QString("%1  ·  %2").arg(item.title_id, item.app_path));

    // Compatibility badge color
    QString badge_color;
    if (item.compatibility == "InGame" || item.compatibility == "Playable") {
        badge_color = "#2ecc71"; // green
    } else if (item.compatibility == "DoesntBoot" || item.compatibility == "Crash") {
        badge_color = "#e74c3c"; // red
    } else {
        badge_color = "#f39c12"; // orange
    }
    m_compat_badge->setText(item.compatibility.isEmpty() ? "Unknown" : item.compatibility);
    m_compat_badge->setStyleSheet(
        QString("QLabel#compatBadge { background: %1; color: white; font-size: 11px;"
                "  font-weight: bold; padding: 3px 12px; border-radius: 10px; }").arg(badge_color)
    );

    UpdateBackdrop();
}

void GameLaunchScreen::UpdateBackdrop() {
    if (m_current_game.icon_path.isEmpty()) {
        m_backdrop->setStyleSheet("background: #0a0a14;");
        return;
    }

    QPixmap pixmap(m_current_game.icon_path);
    if (pixmap.isNull()) {
        m_backdrop->setStyleSheet("background: #0a0a14;");
        return;
    }

    // Scale to fill the screen, then darken for text readability
    QPixmap scaled = pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    // Darken the pixmap by painting a semi-transparent overlay
    QPixmap darkened(scaled.size());
    darkened.fill(Qt::transparent);
    QPainter painter(&darkened);
    painter.drawPixmap(0, 0, scaled);
    painter.fillRect(scaled.rect(), QColor(10, 10, 20, 180)); // 70% opacity dark overlay
    painter.end();

    m_backdrop->setPixmap(darkened);
    m_backdrop->resize(size());
}

void GameLaunchScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_backdrop->resize(size());
    if (!m_current_game.title.isEmpty()) {
        UpdateBackdrop();
    }
}
