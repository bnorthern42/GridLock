/**
 * @file AppearanceSettingsPage.hpp
 * @brief Declaration of the Appearance & Typography settings page widget.
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QString>
#include <QVector>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class AppearanceSettingsPage
 * @brief Configuration panel for theme selection, dark mode, font sizes, and icon styling.
 */
class AppearanceSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit AppearanceSettingsPage(QWidget* parent = nullptr);

    QString selectedTheme() const;
    bool isDarkMode() const;
    QString fileTreeStyle() const;
    bool colorizeIcons() const;
    QString uiFontFamily() const;
    int uiFontSize() const;
    QString codeFontFamily() const;
    int codeFontSize() const;
    QString memoryMapColorScheme() const;

    void loadFromSettings();
    void updateSwatchPreview(const QString& themeId);

private:
    QComboBox* m_themeCombo = nullptr;
    QVector<QLabel*> m_swatchBoxes;
    QCheckBox* m_darkModeCheck = nullptr;
    QComboBox* m_fileTreeStyleCombo = nullptr;
    QCheckBox* m_colorizeIconsCheck = nullptr;
    QComboBox* m_uiFontFamilyCombo = nullptr;
    QSpinBox* m_uiFontSizeBox = nullptr;
    QComboBox* m_codeFontFamilyCombo = nullptr;
    QSpinBox* m_codeFontSizeBox = nullptr;
    QComboBox* m_memoryMapColorSchemeCombo = nullptr;
};

}  // namespace gridlock::ui
