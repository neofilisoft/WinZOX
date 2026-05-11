#include "gui/main_window.hpp"

#include "gui/dialogs.hpp"

#include "compression/compressor.hpp"
#include "extraction/api/unzox/unzox_api.hpp"
#include "io/volume_reader.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QProcess>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringBuilder>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QToolBar>

#include <vector>

namespace winzox::gui {

namespace {

QString HumanSize(quint64 bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', value < 10.0 ? 2 : 1) + QLatin1Char(' ') + QLatin1String(units[unit]);
}

void ApplyDarkPalette(QApplication* app) {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(32, 32, 32));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(24, 24, 24));
    palette.setColor(QPalette::AlternateBase, QColor(40, 40, 40));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(45, 45, 45));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(0, 120, 212));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app->setPalette(palette);
}

void ApplyLightPalette(QApplication* app) {
    app->setPalette(app->style()->standardPalette());
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setAcceptDrops(true);
    QSettings settings("Neofilisoft", "WinZOX");
    darkTheme_ = settings.value("dark_theme", true).toBool();
    BuildUi();
    BuildActions();
    BuildToolbar();
    BuildStatusBar();
    RefreshTitle();
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        if (darkTheme_) {
            ApplyDarkPalette(app);
        } else {
            ApplyLightPalette(app);
        }
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::BuildUi() {
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Path"), tr("Size"), tr("Compressed"), tr("Algo"), tr("CRC32")});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    setCentralWidget(tree_);
}

void MainWindow::BuildActions() {
    openAction_ = new QAction(tr("&Open…"), this);
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::OnOpenArchive);

    addAction_ = new QAction(tr("&Add"), this);
    addAction_->setShortcut(QKeySequence("Ctrl+N"));
    connect(addAction_, &QAction::triggered, this, &MainWindow::OnAddFiles);

    extractAction_ = new QAction(tr("E&xtract All"), this);
    extractAction_->setShortcut(QKeySequence("Ctrl+E"));
    connect(extractAction_, &QAction::triggered, this, &MainWindow::OnExtractAll);

    extractSelAction_ = new QAction(tr("Extract &Selected"), this);
    extractSelAction_->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(extractSelAction_, &QAction::triggered, this, &MainWindow::OnExtractSelected);

    testAction_ = new QAction(tr("&Test"), this);
    testAction_->setShortcut(QKeySequence("Ctrl+T"));
    connect(testAction_, &QAction::triggered, this, &MainWindow::OnTestArchive);

    settingsAction_ = new QAction(tr("&Settings…"), this);
    connect(settingsAction_, &QAction::triggered, this, &MainWindow::OnSettings);

    themeAction_ = new QAction(tr("Toggle &Theme"), this);
    connect(themeAction_, &QAction::triggered, this, &MainWindow::OnToggleTheme);

    aboutAction_ = new QAction(tr("&About"), this);
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::OnAbout);
}

void MainWindow::BuildToolbar() {
    auto* tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));
    tb->addAction(openAction_);
    tb->addAction(addAction_);
    tb->addAction(extractAction_);
    tb->addAction(extractSelAction_);
    tb->addAction(testAction_);
    tb->addSeparator();
    tb->addAction(settingsAction_);
    tb->addAction(themeAction_);
    tb->addSeparator();
    tb->addAction(aboutAction_);

    auto* menuBar = this->menuBar();
    auto* fileMenu = menuBar->addMenu(tr("&File"));
    fileMenu->addAction(openAction_);
    fileMenu->addAction(addAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(extractAction_);
    fileMenu->addAction(extractSelAction_);
    fileMenu->addAction(testAction_);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar->addMenu(tr("&View"));
    viewMenu->addAction(themeAction_);

    auto* toolsMenu = menuBar->addMenu(tr("&Tools"));
    toolsMenu->addAction(settingsAction_);

    auto* helpMenu = menuBar->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAction_);
}

void MainWindow::BuildStatusBar() {
    statusLabel_ = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(statusLabel_, 1);
}

void MainWindow::RefreshTitle() {
    QString base = tr("WinZOX 3.0.0");
    if (!archivePath_.isEmpty()) {
        base = QFileInfo(archivePath_).fileName() + QLatin1String(" — ") + base;
    }
    setWindowTitle(base);
}

QString MainWindow::ResolveZoxBinary() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
#ifdef Q_OS_WIN
        appDir + QLatin1String("/zox.exe"),
        appDir + QLatin1String("/../zox.exe"),
#else
        appDir + QLatin1String("/zox"),
        appDir + QLatin1String("/../zox"),
        QStringLiteral("/usr/bin/zox"),
        QStringLiteral("/usr/local/bin/zox"),
#endif
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return QStringLiteral("zox");
}

bool MainWindow::LoadArchive(const QString& archivePath) {
    if (!QFileInfo::exists(archivePath)) {
        QMessageBox::warning(this, tr("Not found"), tr("File does not exist: %1").arg(archivePath));
        return false;
    }

    std::vector<uint8_t> archiveBytes;
    try {
        archiveBytes = winzox::io::ReadAllVolumes(std::filesystem::path(archivePath.toStdString()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Read failed"), QString::fromStdString(error.what()));
        return false;
    }

    QString password;
    using namespace winzox::extraction::api::unzox;
    bool requiresPassword = false;
    auto authStatus = ValidateAuthentication(archiveBytes, password.toStdString(), &requiresPassword);
    if (requiresPassword || authStatus.code == ErrorCode::WZOX_ERR_PASSWORD_REQUIRED ||
        authStatus.code == ErrorCode::WZOX_ERR_AUTH_FAILED ||
        authStatus.code == ErrorCode::WZOX_ERR_DECRYPT_FAILED) {
        PasswordDialog dialog(QFileInfo(archivePath).fileName(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        password = dialog.password();
    }

    archive::ArchiveMetadata metadata;
    std::vector<archive::ArchiveEntryInfo> entries;
    auto status = ProbeArchiveBytes(archiveBytes, password.toStdString(), &metadata, &entries);
    if (!status) {
        QMessageBox::warning(this, tr("Open failed"), QString::fromStdString(status.message));
        return false;
    }

    archivePath_ = archivePath;
    password_ = password;
    tree_->clear();
    for (const auto& entry : entries) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdString(entry.path));
        item->setText(1, HumanSize(entry.originalSize));
        item->setText(2, HumanSize(entry.storedSize));
        item->setText(3, QString::fromStdString(winzox::compression::AlgorithmName(entry.algorithm)));
        item->setText(4, QString::asprintf("%08X", entry.crc32));
        item->setData(0, Qt::UserRole, QString::fromStdString(entry.path));
    }

    statusLabel_->setText(tr("%1 entries — encrypted: %2 — auth: %3")
                              .arg(entries.size())
                              .arg(metadata.encrypted ? tr("yes") : tr("no"))
                              .arg(metadata.authenticated ? tr("yes") : tr("no")));
    RefreshTitle();
    return true;
}

void MainWindow::OnOpenArchive() {
    const QString selected = QFileDialog::getOpenFileName(
        this,
        tr("Open archive"),
        QString(),
        tr("Archives (*.zox *.zip *.7z *.rar);;All files (*)"));
    if (!selected.isEmpty()) {
        LoadArchive(selected);
    }
}

void MainWindow::OnAddFiles() {
    AddArchiveDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto opts = dialog.options();
    if (opts.inputs.isEmpty() || opts.outputBase.isEmpty()) {
        QMessageBox::information(this, tr("Add"), tr("Source and output paths are required"));
        return;
    }

    const QString program = ResolveZoxBinary();
    QStringList args;
    if (opts.inputs.size() != 1) {
        QMessageBox::information(this, tr("Add"),
                                 tr("Only a single source path is supported in this build."));
        return;
    }
    args << QStringLiteral("add") << opts.inputs.first() << opts.outputBase;
    args << QStringLiteral("--format") << opts.format;
    args << QStringLiteral("--algo") << opts.algorithm;
    args << QStringLiteral("--zstd-level") << QString::number(opts.level);
    if (opts.encryption != QLatin1String("none")) {
        args << QStringLiteral("--encrypt") << opts.encryption;
        args << QStringLiteral("-p") << QStringLiteral("env:WZX_GUI_PW");
    }

    QProcess process(this);
    if (opts.encryption != QLatin1String("none")) {
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("WZX_GUI_PW"), opts.password);
        process.setProcessEnvironment(env);
    }
    process.start(program, args);
    if (!process.waitForFinished(3600 * 1000)) {
        QMessageBox::warning(this, tr("Add"), tr("Archive creation timed out"));
        return;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QMessageBox::warning(this, tr("Add"), tr("zox failed: %1").arg(QString::fromUtf8(process.readAllStandardError())));
        return;
    }
    QMessageBox::information(this, tr("Add"), tr("Archive created."));
    QString output = opts.outputBase;
    if (opts.format == QLatin1String("zox") && !output.endsWith(QStringLiteral(".zox"))) {
        output += QStringLiteral(".zox");
    } else if (opts.format == QLatin1String("zip") && !output.endsWith(QStringLiteral(".zip"))) {
        output += QStringLiteral(".zip");
    }
    LoadArchive(output);
}

void MainWindow::OnExtractAll() {
    if (archivePath_.isEmpty()) {
        QMessageBox::information(this, tr("Extract"), tr("Open an archive first."));
        return;
    }
    QFileInfo info(archivePath_);
    ExtractDialog dialog(info.absolutePath(), !password_.isEmpty(), this);
    if (!password_.isEmpty()) {
        // pre-fill (user can clear if they want fresh prompt)
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto opts = dialog.options();
    if (opts.destination.isEmpty()) {
        QMessageBox::information(this, tr("Extract"), tr("Destination is required."));
        return;
    }

    std::vector<uint8_t> archiveBytes;
    try {
        archiveBytes = winzox::io::ReadAllVolumes(std::filesystem::path(archivePath_.toStdString()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Extract"), QString::fromStdString(error.what()));
        return;
    }

    using namespace winzox::extraction::api::unzox;
    const std::string password = opts.password.isEmpty() ? password_.toStdString() : opts.password.toStdString();
    const auto status = ExtractToDirectory(archiveBytes, opts.destination.toStdString(), password);
    if (!status) {
        QMessageBox::warning(this, tr("Extract"), QString::fromStdString(status.message));
        return;
    }
    QMessageBox::information(this, tr("Extract"), tr("Extraction complete."));
}

void MainWindow::OnExtractSelected() {
    if (archivePath_.isEmpty()) {
        QMessageBox::information(this, tr("Extract"), tr("Open an archive first."));
        return;
    }
    const auto items = tree_->selectedItems();
    if (items.isEmpty()) {
        OnExtractAll();
        return;
    }

    const QString destDir = QFileDialog::getExistingDirectory(this, tr("Destination folder"));
    if (destDir.isEmpty()) {
        return;
    }

    QString password = password_;
    std::vector<uint8_t> archiveBytes;
    try {
        archiveBytes = winzox::io::ReadAllVolumes(std::filesystem::path(archivePath_.toStdString()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Extract"), QString::fromStdString(error.what()));
        return;
    }

    using namespace winzox::extraction::api::unzox;
    archive::ArchiveMetadata meta;
    std::vector<archive::ArchiveEntryInfo> entries;
    auto status = ProbeArchiveBytes(archiveBytes, password.toStdString(), &meta, &entries);
    if (!status) {
        QMessageBox::warning(this, tr("Extract"), QString::fromStdString(status.message));
        return;
    }

    int extracted = 0;
    for (auto* item : items) {
        const QString itemPath = item->data(0, Qt::UserRole).toString();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (QString::fromStdString(entries[i].path) == itemPath) {
                std::vector<uint8_t> buffer;
                auto entryStatus = ReadEntryBytes(archiveBytes, i, &buffer, password.toStdString());
                if (!entryStatus) {
                    QMessageBox::warning(this, tr("Extract"),
                                         tr("Failed: %1\n%2").arg(itemPath, QString::fromStdString(entryStatus.message)));
                    return;
                }
                const QString outPath = destDir + QLatin1Char('/') + QFileInfo(itemPath).fileName();
                QFile out(outPath);
                if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QMessageBox::warning(this, tr("Extract"), tr("Cannot write %1").arg(outPath));
                    return;
                }
                out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<qint64>(buffer.size()));
                ++extracted;
                break;
            }
        }
    }

    QMessageBox::information(this, tr("Extract"), tr("Extracted %1 entries.").arg(extracted));
}

void MainWindow::OnTestArchive() {
    if (archivePath_.isEmpty()) {
        QMessageBox::information(this, tr("Test"), tr("Open an archive first."));
        return;
    }
    std::vector<uint8_t> archiveBytes;
    try {
        archiveBytes = winzox::io::ReadAllVolumes(std::filesystem::path(archivePath_.toStdString()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Test"), QString::fromStdString(error.what()));
        return;
    }
    using namespace winzox::extraction::api::unzox;
    const auto status = ValidateAuthentication(archiveBytes, password_.toStdString());
    if (!status) {
        QMessageBox::warning(this, tr("Test"), QString::fromStdString(status.message));
        return;
    }
    QMessageBox::information(this, tr("Test"), tr("Archive integrity OK."));
}

void MainWindow::OnSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        if (dialog.darkTheme() != darkTheme_) {
            darkTheme_ = dialog.darkTheme();
            if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
                if (darkTheme_) {
                    ApplyDarkPalette(app);
                } else {
                    ApplyLightPalette(app);
                }
            }
        }
    }
}

void MainWindow::OnToggleTheme() {
    darkTheme_ = !darkTheme_;
    QSettings settings("Neofilisoft", "WinZOX");
    settings.setValue("dark_theme", darkTheme_);
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        if (darkTheme_) {
            ApplyDarkPalette(app);
        } else {
            ApplyLightPalette(app);
        }
    }
}

void MainWindow::OnAbout() {
    QMessageBox::about(this, tr("About WinZOX"),
        tr("<h3>WinZOX 3.0.0</h3>"
           "<p>Modern archiver with Gorgon-AEAD cascade encryption "
           "(ChaCha20-Poly1305 over AES-256-GCM) and Fluent-style desktop UI.</p>"
           "<p>Build: %1 (Qt %2)</p>")
            .arg(QStringLiteral(__DATE__))
            .arg(QStringLiteral(QT_VERSION_STR)));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    LoadArchive(urls.first().toLocalFile());
}

} // namespace winzox::gui
