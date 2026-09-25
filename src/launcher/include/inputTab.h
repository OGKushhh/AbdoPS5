#ifndef LAUNCHER_INCLUDE_INPUT_TAB_H_
#define LAUNCHER_INCLUDE_INPUT_TAB_H_

#include <QLabel>
#include <QPushButton>
#include <QWidget>

// Kyty-UI: Input mapping tab with controller diagram + button list.
// Uses a placeholder controller image until the DualSense photos arrive.

class InputTab : public QWidget {
    Q_OBJECT

public:
    explicit InputTab(QWidget* parent = nullptr);
    ~InputTab() override = default;

private:
    void SetupUi();
    QWidget* CreateControllerDiagram();
    QWidget* CreateButtonMappingList();
    QWidget* CreateSliderSettings();

    QLabel* m_controller_image = nullptr;
};

#endif // LAUNCHER_INCLUDE_INPUT_TAB_H_
