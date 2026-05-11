#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTreeWidget>

#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QAction;
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

namespace winzox::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool LoadArchive(const QString& archivePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void OnOpenArchive();
    void OnAddFiles();
    void OnExtractAll();
    void OnExtractSelected();
    void OnTestArchive();
    void OnAbout();
    void OnSettings();
    void OnToggleTheme();

private:
    void BuildUi();
    void BuildActions();
    void BuildToolbar();
    void BuildStatusBar();
    void RefreshTitle();
    QString ResolveZoxBinary() const;

    QTreeWidget* tree_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QString archivePath_;
    QString password_;
    bool darkTheme_ = true;

    QAction* openAction_ = nullptr;
    QAction* addAction_ = nullptr;
    QAction* extractAction_ = nullptr;
    QAction* extractSelAction_ = nullptr;
    QAction* testAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* themeAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
};

} // namespace winzox::gui
