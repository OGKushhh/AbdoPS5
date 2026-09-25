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

// Kyty-UI: Frameless window with proper layout — title bar on top,
// central widget below, all in a vertical container.

namespace {

constexpr const char* kTitleBarQss = R"(
    QWidget#titleBar {
        background: #07080b;
        border-bottom: 1px solid #1f2330;
        min-height: 40px;
        max-height: 40px;
    }
    QLabel#titleText {
        color: #a8b0bd;
        font-size: 12px;
        font-weight: 500;
        padding-left: 4px;
    }
    QToolButton#windowBtn {
        background: transparent;
        border: none;
        width: 46px;
        height: 40px;
        color: #6b7280;
        font-size: 14px;
    }
    QToolButton#windowBtn:hover {
        background: #161922;
        color: #f3f5f8;
    }
    QToolButton#closeBtn:hover {
        background: #e74c3c;
        color: white;
    }
)";

} // namespace

FramelessWindow::FramelessWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setStyleSheet("QMainWindow { background: #07080b; }");

    // Create a container that holds title bar + central content vertically
    auto* container = new QWidget(this);
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    SetupTitleBar();
    containerLayout->addWidget(m_title_bar);

    // Create a placeholder central widget that subclasses will replace
    // via setCentralWidget(). We use an event filter approach instead.
    // For now, just use QMainWindow's central widget mechanism.
    // The title bar stays on top because it's in the container layout.

    // We can't use setCentralWidget(container) here because subclasses
    // call setCentralWidget() later. Instead, we'll override the central
    // widget by having the title bar as a top-level child that's
    // manually positioned in resizeEvent.

    // Actually, the cleanest approach: use QMainWindow but manually
    // position the title bar at the top and offset the central widget.
    // We do this by overriding setCentralWidget to wrap it.

    setCentralWidget(container);
    m_title_bar->setParent(container);
    m_title_bar->show();
}

void FramelessWindow::SetupTitleBar() {
    m_title_bar = new QWidget();
    m_title_bar->setObjectName("titleBar");
    m_title_bar->setFixedHeight(40);
    m_title_bar->setStyleSheet(kTitleBarQss);

    auto* layout = new QHBoxLayout(m_title_bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // App title
    auto* title_label = new QLabel("  AbDoPS5", m_title_bar);
    title_label->setObjectName("titleText");
    layout->addWidget(title_label);
    layout->addStretch();

    // Minimize
    auto* min_btn = new QToolButton(m_title_bar);
    min_btn->setObjectName("windowBtn");
    min_btn->setText("\xE2\x80\x93"); // em dash as minimize icon
    min_btn->setToolTip("Minimize");
    layout->addWidget(min_btn);

    // Maximize/restore
    auto* max_btn = new QToolButton(m_title_bar);
    max_btn->setObjectName("windowBtn");
    max_btn->setText("\xE2\x96\xA1"); // square as maximize icon
    max_btn->setToolTip("Maximize");
    layout->addWidget(max_btn);

    // Close
    auto* close_btn = new QToolButton(m_title_bar);
    close_btn->setObjectName("closeBtn");
    close_btn->setText("\xC3\x97"); // multiplication sign as close icon
    close_btn->setToolTip("Close");
    layout->addWidget(close_btn);

    connect(min_btn, &QToolButton::clicked, this, &QMainWindow::showMinimized);
    connect(max_btn, &QToolButton::clicked, this, [this, max_btn]() {
        if (m_maximized) {
            restoreGeometry(m_saved_geometry);
            showNormal();
            max_btn->setText("\xE2\x96\xA1");
            m_maximized = false;
        } else {
            m_saved_geometry = saveGeometry();
            showMaximized();
            max_btn->setText("\xE2\x9D\x96");
            m_maximized = true;
        }
    });
    connect(close_btn, &QToolButton::clicked, this, &QMainWindow::close);
}

void FramelessWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_title_bar && m_title_bar->underMouse()) {
        // Check if the click is on the title bar (not on a button)
        auto* child = m_title_bar->childAt(m_title_bar->mapFrom(this, event->pos()));
        if (!child || !qobject_cast<QToolButton*>(child)) {
            m_dragging = true;
            m_drag_offset = event->globalPos() - frameGeometry().topLeft();
        }
    }
    QMainWindow::mousePressEvent(event);
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
    if (event->button() == Qt::LeftButton && m_title_bar && m_title_bar->underMouse()) {
        auto* child = m_title_bar->childAt(m_title_bar->mapFrom(this, event->pos()));
        if (!child || !qobject_cast<QToolButton*>(child)) {
            if (isMaximized()) {
                showNormal();
            } else {
                showMaximized();
            }
            event->accept();
        }
    }
    QMainWindow::mouseDoubleClickEvent(event);
}
