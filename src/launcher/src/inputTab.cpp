#include "inputTab.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

// Kyty-UI: Input mapping tab — controller diagram + button list + sliders.
// The controller image will be replaced with the DualSense photos once
// they arrive. For now, a placeholder with CSS-drawn controller outline.

InputTab::InputTab(QWidget* parent) : QWidget(parent) {
    SetupUi();
}

void InputTab::SetupUi() {
    setStyleSheet(
        "QWidget#tabContent { background: #0f0f1a; }"
        "QLabel { color: #ccddee; font-size: 13px; }"
        "QLabel#sectionTitle { color: #1a9fff; font-size: 15px; font-weight: bold; }"
        "QLabel#settingLabel { color: #aabbcc; font-size: 12px; }"
        "QLabel#settingDesc { color: #667788; font-size: 11px; }"
        "QLabel#controllerPlaceholder {"
        "  background: #1a1a2e;"
        "  border: 2px dashed #334455;"
        "  border-radius: 20px;"
        "  color: #445566;"
        "  font-size: 14px;"
        "  qproperty-alignment: AlignCenter;"
        "}"
        "QSlider::groove:horizontal {"
        "  height: 6px; background: #334455; border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 16px; height: 16px; background: #1a9fff;"
        "  border-radius: 8px; margin: -5px 0;"
        "}"
        "QTreeWidget {"
        "  background: #0f0f1a; color: #ccddee; border: 1px solid #1a1a2e;"
        "  font-size: 12px;"
        "}"
        "QTreeWidget::item { padding: 4px 8px; }"
        "QTreeWidget::item:selected { background: #16213e; color: #1a9fff; }"
        "QHeaderView::section {"
        "  background: #1a1a2e; color: #8899aa; border: none;"
        "  padding: 6px 8px; font-size: 11px;"
        "}"
    );

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget();
    content->setObjectName("tabContent");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(8);

    // Section title
    auto* title = new QLabel("Input Mapping", content);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);

    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background: #1a1a2e; max-height: 1px;");
    layout->addWidget(sep);
    layout->addSpacing(8);

    // Controller diagram (placeholder)
    layout->addWidget(CreateControllerDiagram());

    // Button mapping list
    layout->addWidget(CreateButtonMappingList());

    // Slider settings
    layout->addWidget(CreateSliderSettings());

    layout->addStretch();
    scroll->setWidget(content);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

QWidget* InputTab::CreateControllerDiagram() {
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 8, 0, 8);

    // Placeholder controller image — will be replaced with DualSense photo
    m_controller_image = new QLabel(container);
    m_controller_image->setObjectName("controllerPlaceholder");
    m_controller_image->setText("🎮\nDualSense Controller\n\n(Photos pending upload)\n"
                                 "Will show button mapping overlay\n"
                                 "with clickable hotspots");
    m_controller_image->setMinimumSize(400, 250);
    m_controller_image->setMaximumHeight(300);
    layout->addWidget(m_controller_image, 1);

    return container;
}

QWidget* InputTab::CreateButtonMappingList() {
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 8, 0, 8);

    auto* title = new QLabel("Button Bindings", container);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);

    auto* tree = new QTreeWidget(container);
    tree->setColumnCount(3);
    tree->setHeaderLabels({"PS5 Button", "Host Binding", "Status"});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(false);
    tree->setMinimumHeight(300);

    // Standard DualSense button list
    const QStringList buttons = {
        "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
        "Left Stick Up", "Left Stick Down", "Left Stick Left", "Left Stick Right",
        "Right Stick Up", "Right Stick Down", "Right Stick Left", "Right Stick Right",
        "Cross (✕)", "Circle (○)", "Square (□)", "Triangle (△)",
        "L1", "R1", "L2", "R2",
        "L3 (Click)", "R3 (Click)",
        "Share", "Options", "PS Button", "Touchpad Click",
        "L2 Trigger", "R2 Trigger",
    };

    for (const auto& btn : buttons) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, btn);
        item->setText(1, "— Not mapped —");
        item->setText(2, "⚠");
        item->setForeground(1, QBrush(QColor("#667788")));
        item->setForeground(2, QBrush(QColor("#e67e22")));
    }

    layout->addWidget(tree);
    return container;
}

QWidget* InputTab::CreateSliderSettings() {
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 8, 0, 8);

    auto* title = new QLabel("Analog Settings", container);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);
    layout->addSpacing(4);

    // Deadzone sliders
    const auto create_slider_row = [](const QString& label, int min, int max,
                                      int val) -> QWidget* {
        auto* row = new QWidget();
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 4, 0, 4);
        auto* lbl = new QLabel(label, row);
        lbl->setObjectName("settingLabel");
        lbl->setMinimumWidth(180);
        l->addWidget(lbl);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(min, max);
        slider->setValue(val);
        l->addWidget(slider, 1);
        auto* val_label = new QLabel(QString::number(val) + "%", row);
        val_label->setObjectName("settingLabel");
        val_label->setMinimumWidth(50);
        QObject::connect(slider, &QSlider::valueChanged, val_label,
                         [val_label](int v) { val_label->setText(QString::number(v) + "%"); });
        l->addWidget(val_label);
        return row;
    };

    layout->addWidget(create_slider_row("Left Stick Deadzone", 0, 50, 10));
    layout->addWidget(create_slider_row("Right Stick Deadzone", 0, 50, 10));
    layout->addWidget(create_slider_row("Left Trigger Deadzone", 0, 50, 5));
    layout->addWidget(create_slider_row("Right Trigger Deadzone", 0, 50, 5));

    // Rumble intensity
    layout->addSpacing(8);
    layout->addWidget(create_slider_row("Rumble Intensity", 0, 100, 100));

    // Lightbar color (placeholder — would be a color picker)
    layout->addSpacing(8);
    auto* lightbar_label = new QLabel("Lightbar Color: Blue (default)", container);
    lightbar_label->setObjectName("settingLabel");
    layout->addWidget(lightbar_label);

    return container;
}
