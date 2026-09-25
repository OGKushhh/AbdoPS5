#ifndef MAIN_DIALOG_H
#define MAIN_DIALOG_H

#include <QMainWindow>
#include <QString>

class QWidget;
class QProcess;
class MainDialogPrivate;
class QSettings;
class QResizeEvent;

class Configuration;

// Renamed base class from QDialog to QMainWindow so we get a built-in
// menuBar() and statusBar(). The previous QDialog + manual menu bar
// approach was the root cause of the broken launcher — the menu bar had
// to be shoehorned into a dialog layout, and the QStackedWidget attempt
// to unify the UI reparented widgets created by setupUi(), which
// nuked them on first resize.
//
// With QMainWindow we get a clean central-widget + menu-bar + status-bar
// layout for free, and we build the central widget (sidebar + QStackedWidget
// with List/Grid/Settings pages) entirely in C++ — no .ui file, no
// reparenting, no layout fighting.
class MainDialog: public QMainWindow {
        Q_OBJECT

signals:
        void Start();
        void Resize();

public:
        explicit MainDialog(QWidget* parent = nullptr);
        ~MainDialog() override = default;

        void RunInterpreter(QProcess* process, const Configuration& info);

        static void WriteSettings(QSettings& s);
        static void ReadSettings(QSettings& s);

        void resizeEvent(QResizeEvent* event) override;

private:
        MainDialogPrivate* m_p = nullptr;
};

#endif // MAIN_DIALOG_H
