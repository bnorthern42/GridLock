/**
 * @file TerminalDocsetsPages.hpp
 * @brief Declarations of DocsetSettingsPage and TerminalSettingsPage widgets.
 */

#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QTableWidget>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class DocsetSettingsPage
 * @brief Configuration panel for Dash/Zeal offline API docset storage and discovery.
 */
class DocsetSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit DocsetSettingsPage(QWidget* parent = nullptr);

    QString docsetDirectory() const;
    void loadFromSettings();
    void refreshTable();

private slots:
    void onAutoDetect();

private:
    QLineEdit* m_dirEdit = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_autoDetectBtn = nullptr;
};

/**
 * @class TerminalSettingsPage
 * @brief Configuration panel for embedded shell path, font family, and font size.
 */
class TerminalSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit TerminalSettingsPage(QWidget* parent = nullptr);

    QString shellPath() const;
    QString fontFamily() const;
    int fontSize() const;

    void loadFromSettings();

private:
    QLineEdit* m_shellPathEdit = nullptr;
    QComboBox* m_fontFamilyCombo = nullptr;
    QSpinBox* m_fontSizeBox = nullptr;
};

}  // namespace gridlock::ui
