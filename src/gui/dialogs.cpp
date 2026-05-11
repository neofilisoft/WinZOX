#include "gui/dialogs.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

namespace winzox::gui {

namespace {
const QStringList kEncryptionModes = {
    QStringLiteral("none"),
    QStringLiteral("gorgon-aead"),
    QStringLiteral("aes-gcm"),
    QStringLiteral("chacha20"),
    QStringLiteral("aes-cbc"),
    QStringLiteral("gorgon-cbc"),
};
const QStringList kCompressionModes = {
    QStringLiteral("zstd"),
    QStringLiteral("zlib"),
    QStringLiteral("lz4"),
    QStringLiteral("lzma2"),
    QStringLiteral("store"),
};
const QStringList kFormats = {
    QStringLiteral("zox"),
    QStringLiteral("zip"),
};
} // namespace

// ---------------------------------------------------------------------------
PasswordDialog::PasswordDialog(const QString& archiveLabel, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Password required"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);

    auto* label = new QLabel(tr("Enter the password for: %1").arg(archiveLabel), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    edit_ = new QLineEdit(this);
    edit_->setEchoMode(QLineEdit::Password);
    layout->addWidget(edit_);

    showCheck_ = new QCheckBox(tr("Show password"), this);
    layout->addWidget(showCheck_);
    connect(showCheck_, &QCheckBox::toggled, this, [this](bool on) {
        edit_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    edit_->setFocus();
}

QString PasswordDialog::password() const { return edit_->text(); }

// ---------------------------------------------------------------------------
AddArchiveDialog::AddArchiveDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Create new archive"));
    setModal(true);
    resize(540, 320);
    QSettings settings("Neofilisoft", "WinZOX");

    auto* form = new QFormLayout();

    inputsEdit_ = new QLineEdit(this);
    inputsEdit_->setPlaceholderText(tr("Files or folders to add (semicolon-separated)"));
    auto* inputBrowse = new QToolButton(this);
    inputBrowse->setText(QStringLiteral("…"));
    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(inputsEdit_);
    inputRow->addWidget(inputBrowse);
    form->addRow(tr("Source:"), inputRow);
    connect(inputBrowse, &QToolButton::clicked, this, &AddArchiveDialog::BrowseInputs);

    outputEdit_ = new QLineEdit(this);
    outputEdit_->setPlaceholderText(tr("Output base path (without extension)"));
    auto* outputBrowse = new QToolButton(this);
    outputBrowse->setText(QStringLiteral("…"));
    auto* outputRow = new QHBoxLayout();
    outputRow->addWidget(outputEdit_);
    outputRow->addWidget(outputBrowse);
    form->addRow(tr("Output:"), outputRow);
    connect(outputBrowse, &QToolButton::clicked, this, &AddArchiveDialog::BrowseOutput);

    formatCombo_ = new QComboBox(this);
    formatCombo_->addItems(kFormats);
    form->addRow(tr("Format:"), formatCombo_);

    algorithmCombo_ = new QComboBox(this);
    algorithmCombo_->addItems(kCompressionModes);
    algorithmCombo_->setCurrentText(settings.value("default_algorithm", "zstd").toString());
    form->addRow(tr("Compression:"), algorithmCombo_);

    levelSpin_ = new QSpinBox(this);
    levelSpin_->setRange(1, 22);
    levelSpin_->setValue(9);
    form->addRow(tr("Level:"), levelSpin_);

    encryptionCombo_ = new QComboBox(this);
    encryptionCombo_->addItems(kEncryptionModes);
    encryptionCombo_->setCurrentText(settings.value("default_encryption", "gorgon-aead").toString());
    form->addRow(tr("Encryption:"), encryptionCombo_);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    showPasswordCheck_ = new QCheckBox(tr("show"), this);
    auto* passwordRow = new QHBoxLayout();
    passwordRow->addWidget(passwordEdit_);
    passwordRow->addWidget(showPasswordCheck_);
    form->addRow(tr("Password:"), passwordRow);
    connect(showPasswordCheck_, &QCheckBox::toggled, this, &AddArchiveDialog::TogglePasswordVisibility);

    solidCheck_ = new QCheckBox(tr("Solid mode"), this);
    solidCheck_->setChecked(true);
    form->addRow(QString(), solidCheck_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

AddOptions AddArchiveDialog::options() const {
    AddOptions opts;
    const QString rawInputs = inputsEdit_->text();
    for (const QString& part : rawInputs.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        opts.inputs.append(part.trimmed());
    }
    opts.outputBase = outputEdit_->text().trimmed();
    opts.format = formatCombo_->currentText();
    opts.algorithm = algorithmCombo_->currentText();
    opts.encryption = encryptionCombo_->currentText();
    opts.password = passwordEdit_->text();
    opts.solid = solidCheck_->isChecked();
    opts.level = levelSpin_->value();
    return opts;
}

void AddArchiveDialog::BrowseInputs() {
    const QString selected = QFileDialog::getOpenFileName(this, tr("Select file to add"));
    if (!selected.isEmpty()) {
        if (!inputsEdit_->text().isEmpty()) {
            inputsEdit_->setText(inputsEdit_->text() + QLatin1String("; ") + selected);
        } else {
            inputsEdit_->setText(selected);
        }
    }
}

void AddArchiveDialog::BrowseOutput() {
    const QString selected = QFileDialog::getSaveFileName(
        this, tr("Output archive"), outputEdit_->text(),
        tr("WinZOX archive (*.zox);;Zip archive (*.zip);;All files (*)"));
    if (!selected.isEmpty()) {
        QFileInfo info(selected);
        outputEdit_->setText(info.absoluteFilePath());
    }
}

void AddArchiveDialog::TogglePasswordVisibility() {
    passwordEdit_->setEchoMode(showPasswordCheck_->isChecked() ? QLineEdit::Normal : QLineEdit::Password);
}

// ---------------------------------------------------------------------------
ExtractDialog::ExtractDialog(const QString& defaultDestination, bool requiresPassword, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Extract archive"));
    setModal(true);
    resize(520, 220);

    auto* form = new QFormLayout();

    destinationEdit_ = new QLineEdit(defaultDestination, this);
    auto* browse = new QToolButton(this);
    browse->setText(QStringLiteral("…"));
    auto* row = new QHBoxLayout();
    row->addWidget(destinationEdit_);
    row->addWidget(browse);
    form->addRow(tr("Destination:"), row);
    connect(browse, &QToolButton::clicked, this, &ExtractDialog::BrowseDestination);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    if (!requiresPassword) {
        passwordEdit_->setPlaceholderText(tr("(leave blank if not encrypted)"));
    }
    showPasswordCheck_ = new QCheckBox(tr("show"), this);
    auto* passwordRow = new QHBoxLayout();
    passwordRow->addWidget(passwordEdit_);
    passwordRow->addWidget(showPasswordCheck_);
    form->addRow(tr("Password:"), passwordRow);
    connect(showPasswordCheck_, &QCheckBox::toggled, this, [this](bool on) {
        passwordEdit_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });

    keepBrokenCheck_ = new QCheckBox(tr("Keep broken files (best effort)"), this);
    form->addRow(QString(), keepBrokenCheck_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ExtractOptions ExtractDialog::options() const {
    ExtractOptions opts;
    opts.destination = destinationEdit_->text();
    opts.password = passwordEdit_->text();
    opts.keepBrokenFiles = keepBrokenCheck_->isChecked();
    return opts;
}

void ExtractDialog::BrowseDestination() {
    const QString selected = QFileDialog::getExistingDirectory(this, tr("Destination folder"), destinationEdit_->text());
    if (!selected.isEmpty()) {
        destinationEdit_->setText(selected);
    }
}

// ---------------------------------------------------------------------------
SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(420, 200);
    QSettings settings("Neofilisoft", "WinZOX");

    auto* form = new QFormLayout();

    encryptionCombo_ = new QComboBox(this);
    encryptionCombo_->addItems(kEncryptionModes);
    encryptionCombo_->setCurrentText(settings.value("default_encryption", "gorgon-aead").toString());
    form->addRow(tr("Default encryption:"), encryptionCombo_);

    algorithmCombo_ = new QComboBox(this);
    algorithmCombo_->addItems(kCompressionModes);
    algorithmCombo_->setCurrentText(settings.value("default_algorithm", "zstd").toString());
    form->addRow(tr("Default algorithm:"), algorithmCombo_);

    darkThemeCheck_ = new QCheckBox(tr("Use dark Fluent theme"), this);
    darkThemeCheck_->setChecked(settings.value("dark_theme", true).toBool());
    form->addRow(QString(), darkThemeCheck_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, [this, &settings]() {
        QSettings persisted("Neofilisoft", "WinZOX");
        persisted.setValue("default_encryption", encryptionCombo_->currentText());
        persisted.setValue("default_algorithm", algorithmCombo_->currentText());
        persisted.setValue("dark_theme", darkThemeCheck_->isChecked());
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString SettingsDialog::defaultEncryption() const { return encryptionCombo_->currentText(); }
QString SettingsDialog::defaultAlgorithm() const { return algorithmCombo_->currentText(); }
bool SettingsDialog::darkTheme() const { return darkThemeCheck_->isChecked(); }

} // namespace winzox::gui
