/**
 * @file FileIconProvider.cpp
 * @brief High-DPI theme-aware file system icon provider implementation.
 *
 * Implements FileIconProvider, rendering Nerd Font glyphs into QPixmaps
 * for file trees with file extension recognition and theme-based color accents.
 */

#include "FileIconProvider.hpp"
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QSettings>

namespace gridlock::ui {

/**
 * @brief Constructs the FileIconProvider.
 */
FileIconProvider::FileIconProvider() : QAbstractFileIconProvider() {}

/**
 * @brief Generates a themed Nerd Font icon for a specific file or folder.
 *
 * Uses file extensions (.cpp, .hpp, .py, .md, .toml, .json, meson.build) to choose
 * appropriate glyphs and colors (blue, yellow, gray, purple).
 *
 * @param info QFileInfo object describing the filesystem node.
 * @return 16x16 rendered QIcon.
 */
QIcon FileIconProvider::icon(const QFileInfo &info) const {
    // Allocate transparent 16x16 canvas
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Use Symbols Nerd Font for consistent developer icon glyphs
    QFont font("Symbols Nerd Font", 12);
    painter.setFont(font);

    QSettings s("gridlock", "debugger");
    bool colorize = s.value("appearance/colorize_icons", true).toBool();

    QColor iconColor = QColor("#cdd6f4");
    QString unicodeStr = "\uf15b"; // Default document file glyph

    if (info.isDir()) {
        unicodeStr = "\uf07b"; // Folder closed glyph
        if (colorize) iconColor = QColor("#89b4fa"); // Pastel blue
    } else {
        QString ext = info.suffix().toLower();
        QString name = info.fileName().toLower();

        // Recognize C/C++ source and header files
        if (ext == "c" || ext == "cpp" || ext == "h" || ext == "hpp") {
            unicodeStr = "\ue61d"; // C/C++ logo glyph
            if (colorize) iconColor = QColor("#89b4fa"); // Blue
        } else if (ext == "py") {
            unicodeStr = "\ue311"; // Python logo glyph
            if (colorize) iconColor = QColor("#f9e2af"); // Pastel yellow
        } else if (ext == "md") {
            unicodeStr = "\uf48a"; // Markdown document glyph
            if (colorize) iconColor = QColor("#bac2de"); // Muted lavender grey
        } else if (name == "meson.build" || ext == "toml" || ext == "json") {
            unicodeStr = "\uf013"; // Settings gear glyph
            if (colorize) iconColor = QColor("#cba6f7"); // Purple accent
        }
    }

    // Paint glyph centered in 16x16 pixmap
    painter.setPen(iconColor);
    painter.drawText(0, 0, 16, 16, Qt::AlignCenter, unicodeStr);
    return QIcon(pixmap);
}

/**
 * @brief Generates an icon for standard abstract icon types (e.g. Folder, File).
 * @param type Abstract IconType value.
 * @return 16x16 rendered QIcon.
 */
QIcon FileIconProvider::icon(IconType type) const {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont font("Symbols Nerd Font", 12);
    painter.setFont(font);

    QSettings s("gridlock", "debugger");
    bool colorize = s.value("appearance/colorize_icons", true).toBool();
  
    QColor iconColor = QColor("#cdd6f4");
    QString unicodeStr = "\uf15b"; // Default file glyph
  
    if (type == Folder) {
        unicodeStr = "\uf07b"; // Standard folder glyph
        if (colorize) iconColor = QColor("#89b4fa");
    }

    // Paint standard icon glyph
    painter.setPen(iconColor);
    painter.drawText(0, 0, 16, 16, Qt::AlignCenter, unicodeStr);
    return QIcon(pixmap);
}

}  // namespace gridlock::ui

