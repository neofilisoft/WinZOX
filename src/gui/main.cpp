#include "gui/main_window.hpp"

#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>

namespace {
const char* const kFluentStylesheet = R"qss(
QMainWindow, QDialog {
    background: palette(window);
}
QToolBar {
    spacing: 6px;
    padding: 4px;
    border: none;
}
QToolBar QToolButton {
    border-radius: 6px;
    padding: 6px 10px;
}
QToolBar QToolButton:hover {
    background: rgba(127,127,127,0.2);
}
QToolBar QToolButton:pressed {
    background: rgba(127,127,127,0.35);
}
QTreeWidget, QTableWidget, QListWidget {
    border: 1px solid palette(mid);
    border-radius: 6px;
    selection-background-color: palette(highlight);
}
QTreeWidget::item {
    padding: 4px 8px;
}
QHeaderView::section {
    background: palette(button);
    border: none;
    padding: 6px 8px;
    font-weight: 600;
}
QPushButton {
    border-radius: 6px;
    padding: 6px 14px;
    min-width: 80px;
}
QLineEdit, QComboBox, QSpinBox {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 4px 8px;
    min-height: 22px;
}
QStatusBar {
    border-top: 1px solid palette(mid);
}
)qss";
}

int main(int argc, char* argv[]) {
    QApplication::setOrganizationName("Neofilisoft");
    QApplication::setOrganizationDomain("neofilisoft.com");
    QApplication::setApplicationName("WinZOX");
    QApplication::setApplicationVersion("3.1.0");

    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(QString::fromLatin1(kFluentStylesheet));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("WinZOX 3.1.0 archiver"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archive"),
                                 QStringLiteral("Optional archive file to open at startup"));
    parser.process(app);

    winzox::gui::MainWindow window;
    window.resize(1024, 640);
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.LoadArchive(positional.first());
    }

    return app.exec();
}
