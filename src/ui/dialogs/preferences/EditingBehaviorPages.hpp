/**
 * @file EditingBehaviorPages.hpp
 * @brief Declarations of the Editing and Behavior settings page widgets.
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QString>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class EditingSettingsPage
 * @brief Configuration panel for indentation, tab stops, whitespace, and margin guidelines.
 */
class EditingSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit EditingSettingsPage(QWidget* parent = nullptr);

    int tabWidth() const;
    bool insertSpaces() const;
    bool showWhitespace() const;
    bool wordWrap() const;
    bool showEdgeColumn() const;
    int edgeColumn() const;
    bool rainbowBrackets() const;
    bool dataDocHighlighting() const;
    bool inlineVariableValues() const;

    void loadFromSettings();

private:
    QSpinBox* m_tabWidthBox = nullptr;
    QComboBox* m_indentModeCombo = nullptr;
    QComboBox* m_whitespaceCombo = nullptr;
    QCheckBox* m_wordWrapCheck = nullptr;
    QCheckBox* m_showEdgeColumnCheck = nullptr;
    QSpinBox* m_edgeColumnBox = nullptr;
    QCheckBox* m_rainbowBracketsCheck = nullptr;
    QCheckBox* m_dataDocHighlightingCheck = nullptr;
    QCheckBox* m_inlineVarValuesCheck = nullptr;
};

/**
 * @class BehaviorSettingsPage
 * @brief Configuration panel for session lifecycle and window focus behavior.
 */
class BehaviorSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit BehaviorSettingsPage(QWidget* parent = nullptr);

    QString restoreBreakpoints() const;
    bool confirmQuit() const;
    bool focusOnStop() const;

    void loadFromSettings();

private:
    QComboBox* m_restoreBreakpointsCheck = nullptr;
    QComboBox* m_confirmQuitCheck = nullptr;
    QComboBox* m_focusOnStopCheck = nullptr;
};

}  // namespace gridlock::ui
