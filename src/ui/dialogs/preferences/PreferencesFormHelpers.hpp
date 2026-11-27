/**
 * @file PreferencesFormHelpers.hpp
 * @brief Shared form layout, path-browse row, and font discovery helpers for preference pages.
 */

#pragma once

#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace gridlock::ui::prefs_detail {

/**
 * @brief Returns a deduplicated list of 16+ curated and system-installed monospace font families.
 */
inline QStringList discoverMonospaceFontFamilies() {
    // Seed with 16 curated developer monospace families so headless/CI environments always have >= 15 entries.
    QStringList families = {
        QStringLiteral("JetBrains Mono"),     QStringLiteral("Fira Code"),
        QStringLiteral("FiraCode Nerd Font"), QStringLiteral("Hack"),
        QStringLiteral("Source Code Pro"),    QStringLiteral("Cascadia Code"),
        QStringLiteral("Iosevka"),            QStringLiteral("IBM Plex Mono"),
        QStringLiteral("DejaVu Sans Mono"),   QStringLiteral("Liberation Mono"),
        QStringLiteral("Ubuntu Mono"),        QStringLiteral("Noto Sans Mono"),
        QStringLiteral("Inconsolata"),        QStringLiteral("Victor Mono"),
        QStringLiteral("Terminus"),           QStringLiteral("Monospace")
    };
    // Merge all fixed-pitch or monospace-named system font families discovered via QFontDatabase.
    const QStringList sysFamilies = QFontDatabase::families();
    for (const QString& fam : sysFamilies) {
        const bool isMono = QFontDatabase::isFixedPitch(fam) ||
                            fam.contains(QStringLiteral("Mono"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Code"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Console"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Term"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Nerd"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Courier"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Iosevka"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Hack"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Inconsolata"), Qt::CaseInsensitive);
        if (isMono && !families.contains(fam, Qt::CaseInsensitive)) {
            families.append(fam);
        }
    }
    return families;
}

/**
 * @brief Builds a standardized form layout container with bold heading and 1px separator.
 */
inline QFormLayout* createStandardPageForm(QWidget* parent, const QString& titleHtml,
                                           int spacing = 14) {
    auto* form = new QFormLayout(parent);
    form->setContentsMargins(20, 16, 20, 24);
    form->setSpacing(spacing);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* heading = new QLabel(titleHtml, parent);
    heading->setObjectName(QStringLiteral("heading"));
    form->addRow(heading);

    auto* separator = new QLabel(parent);
    separator->setFixedHeight(1);
    separator->setObjectName(QStringLiteral("separator"));
    form->addRow(separator);
    return form;
}

/**
 * @brief Adds a file browser input row (`QLineEdit` + `Browse…` button) to a form layout.
 */
inline QLineEdit* addBrowsePathRow(QWidget* parent, QFormLayout* form,
                                   const QString& rowLabel, const QString& placeholder,
                                   const QString& toolTip, const QString& dialogTitle,
                                   const QString& defaultDir = QString()) {
    auto* row = new QHBoxLayout();
    auto* edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder);
    if (!toolTip.isEmpty()) edit->setToolTip(toolTip);
    auto* browseBtn = new QPushButton(QObject::tr("Browse…"), parent);
    browseBtn->setMinimumWidth(92);
    QObject::connect(browseBtn, &QPushButton::clicked, parent,
                     [parent, edit, dialogTitle, defaultDir]() {
                         const QString path = QFileDialog::getOpenFileName(
                             parent, dialogTitle, defaultDir);
                         if (!path.isEmpty()) edit->setText(path);
                     });
    row->addWidget(edit);
    row->addWidget(browseBtn);
    form->addRow(rowLabel, row);
    return edit;
}

/**
 * @brief Adds an explanatory footnote label to a form layout.
 */
inline void addFormNote(QWidget* parent, QFormLayout* form, const QString& html) {
    auto* note = new QLabel(html, parent);
    note->setWordWrap(true);
    note->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    form->addRow(note);
}

/**
 * @brief Sets an editable or standard QComboBox value, selecting an existing item if matched.
 */
inline void setComboTextValue(QComboBox* combo, const QString& value) {
    if (!combo) return;
    const int idx = combo->findText(value, Qt::MatchFixedString);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    } else {
        combo->setCurrentIndex(-1);
        combo->setEditText(value);
    }
}

}  // namespace gridlock::ui::prefs_detail
