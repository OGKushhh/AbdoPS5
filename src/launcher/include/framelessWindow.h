#ifndef LAUNCHER_INCLUDE_FRAMELESS_WINDOW_H_
#define LAUNCHER_INCLUDE_FRAMELESS_WINDOW_H_

#include <QMainWindow>
#include <QPoint>

// Kyty-UI: Frameless window with custom title bar (Design A).
// Removes the native OS window decoration and draws our own title bar
// with: app icon, title text, minimize/maximize/close buttons.
// Matches the Steam/Spotify/Discord look.

class FramelessWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit FramelessWindow(QWidget* parent = nullptr);
    ~FramelessWindow() override = default;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void SetupTitleBar();

    QWidget*    m_title_bar    = nullptr;
    QPoint      m_drag_offset;
    bool        m_dragging     = false;
    bool        m_maximized   = false;
    QByteArray  m_saved_geometry;
};

#endif // LAUNCHER_INCLUDE_FRAMELESS_WINDOW_H_
