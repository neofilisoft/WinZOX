#pragma once

#include <QtWidgets/QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
QT_END_NAMESPACE

namespace winzox::gui {

class PasswordDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasswordDialog(const QString& archiveLabel, QWidget* parent = nullptr);
    QString password() const;

private:
    QLineEdit* edit_ = nullptr;
    QCheckBox* showCheck_ = nullptr;
};

struct AddOptions {
    QStringList inputs;
    QString outputBase;
    QString format;
    QString algorithm;
    QString encryption;
    QString password;
    bool solid = true;
    int level = 9;
};

class AddArchiveDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddArchiveDialog(QWidget* parent = nullptr);
    AddOptions options() const;

private slots:
    void BrowseInputs();
    void BrowseOutput();
    void TogglePasswordVisibility();

private:
    QLineEdit* inputsEdit_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QComboBox* algorithmCombo_ = nullptr;
    QComboBox* encryptionCombo_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QCheckBox* showPasswordCheck_ = nullptr;
    QCheckBox* solidCheck_ = nullptr;
    QSpinBox* levelSpin_ = nullptr;
};

struct ExtractOptions {
    QString destination;
    QString password;
    bool keepBrokenFiles = false;
};

class ExtractDialog : public QDialog {
    Q_OBJECT
public:
    ExtractDialog(const QString& defaultDestination, bool requiresPassword, QWidget* parent = nullptr);
    ExtractOptions options() const;

private slots:
    void BrowseDestination();

private:
    QLineEdit* destinationEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QCheckBox* showPasswordCheck_ = nullptr;
    QCheckBox* keepBrokenCheck_ = nullptr;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    QString defaultEncryption() const;
    QString defaultAlgorithm() const;
    bool darkTheme() const;

private:
    QComboBox* encryptionCombo_ = nullptr;
    QComboBox* algorithmCombo_ = nullptr;
    QCheckBox* darkThemeCheck_ = nullptr;
};

} // namespace winzox::gui
