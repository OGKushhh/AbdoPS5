#include "framelessWindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

// Kyty-UI: Frameless window implementation.

namespace {

constexpr const char* kTitleBarQss = R"(
    QWidget#titleBar {
        background: #0a0a14;
        border-bottom: 1px solid #1a1a2e;
        height: 40px;
    }
    QLabel#titleText {
        color: #8899aa;
        font-size: 12px;
        padding-left: 8px;
    }
    QToolButton#windowBtn {
        background: transparent;
        border: none;
        width: 40px;
        height: 40px;
    }
    QToolButton#windowBtn:hover {
        background: #1a1a2e;
    }
    QToolButton#closeBtn:hover {
        background: #e74c3c;
    }
)";

} // namespace

FramelessWindow::FramelessWindow(QWidget* parent) : QMainWindow(parent) {
    // Remove native window decoration
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setStyleSheet("QMainWindow { background: #0a0a14; }");

    SetupTitleBar();
}

void FramelessWindow::SetupTitleBar() {
    m_title_bar = new QWidget(this);
    m_title_bar->setObjectName("titleBar");
    m_title_bar->setFixedHeight(40);
    m_title_bar->setStyleSheet(kTitleBarQss);

    auto* layout = new QHBoxLayout(m_title_bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // App icon + title
    auto* title_label = new QLabel("  AbDoPS5", m_title_bar);
    title_label->setObjectName("titleText");
    layout->addWidget(title_label);
    layout->addStretch();

    // Minimize button
    auto* min_btn = new QToolButton(m_title_bar);
    min_btn->setObjectName("windowBtn");
    min_btn->setText("—");
    min_btn->setToolTip("Minimize");
    layout->addWidget(min_btn);

    // Maximize/restore button
    auto* max_btn = new QToolButton(m_title_bar);
    max_btn->setObjectName("windowBtn");
    max_btn->setText("☐");
    max_btn->setToolTip("Maximize");
    layout->addWidget(max_btn);

    // Close button
    auto* close_btn = new QToolButton(m_title_bar);
    close_btn->setObjectName("closeBtn");
    close_btn->setText("✕");
    close_btn->setToolTip("Close");
    layout->addWidget(close_btn);

    connect(min_btn, &QToolButton::clicked, this, &QMainWindow::showMinimized);
    connect(max_btn, &QToolButton::clicked, this, [this, max_btn]() {
        if (m_maximized) {
            restoreGeometry(m_saved_geometry);
            showNormal();
            max_btn->setText("☐");
            m_maximized = false;
        } else {
            m_saved_geometry = saveGeometry();
            showMaximized();
            max_btn->setText("❖");
            m_maximized = true;
        }
    });
    connect(close_btn, &QToolButton::clicked, this, &QMainWindow::close);

    // Use the title bar as a custom menu bar replacement
    // QMainWindow::menuBar() would draw native; we reparent into our title bar
    auto* menubar = menuBar();
    menubar->setStyleSheet(
        "QMenuBar { background: transparent; color: #8899aa; font-size: 12px; }"
        "QMenuBar::item { padding: 4px 12px; background: transparent; }"
        "QMenuBar::item:selected { background: #1a1a2e; color: #1a9fff; }"
        "QMenu { background: #0a0a14; color: #ccddee; border: 1px solid #1a1a2e; }"
        "QMenu::item:selected { background: #16213e; color: #1a9fff; }"
    );
    // Insert menu bar into the title bar layout (after title, before buttons)
    layout->insertWidget(1, static_cast<QWidget*>(menubar));
}

void FramelessWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_title_bar->underMouse()) {
        m_dragging = true;
        m_drag_offset = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    } else {
        QMainWindow::mousePressEvent(event);
    }
}

void FramelessWindow::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        move(event->globalPos() - m_drag_offset);
        event->accept();
    } else {
        QMainWindow::mouseMoveEvent(event);
    }
}

void FramelessWindow::mouseReleaseEvent(QMouseEvent* event) {
    m_dragging = false;
    QMainWindow::mouseReleaseEvent(event);
}

void FramelessWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_title_bar->underMouse()) {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
        event->accept();
    } else {
        QMainWindow::mouseDoubleClickEvent(event);
    }
}
