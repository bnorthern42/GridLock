#include "FileIconProvider.hpp"
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QSettings>

namespace gridlock::ui {

FileIconProvider::FileIconProvider() : QAbstractFileIconProvider() {}

QIcon FileIconProvider::icon(const QFileInfo &info) const {
  QPixmap pixmap(16, 16);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  QFont font("Symbols Nerd Font", 12);
  painter.setFont(font);

  QSettings s("gridlock", "debugger");
  bool colorize = s.value("appearance/colorize_icons", true).toBool();

  QColor iconColor = QColor("#cdd6f4");
  QString unicodeStr = "\uf15b"; // Default file

  if (info.isDir()) {
    unicodeStr = "\uf07b"; // Folder closed
    if (colorize) iconColor = QColor("#89b4fa"); // Blue
  } else {
    QString ext = info.suffix().toLower();
    QString name = info.fileName().toLower();

    if (ext == "c" || ext == "cpp" || ext == "h" || ext == "hpp") {
      unicodeStr = "\ue61d"; // C/C++ logo
      if (colorize) iconColor = QColor("#89b4fa"); // Blue
    } else if (ext == "py") {
      unicodeStr = "\ue311"; // Python logo
      if (colorize) iconColor = QColor("#f9e2af"); // Yellow
    } else if (ext == "md") {
      unicodeStr = "\uf48a"; // Markdown logo
      if (colorize) iconColor = QColor("#bac2de"); // Grey
    } else if (name == "meson.build" || ext == "toml" || ext == "json") {
      unicodeStr = "\uf013"; // Cog/Settings
      if (colorize) iconColor = QColor("#cba6f7"); // Purple
    }
  }

  painter.setPen(iconColor);
  painter.drawText(0, 0, 16, 16, Qt::AlignCenter, unicodeStr);
  return QIcon(pixmap);
}

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
  QString unicodeStr = "\uf15b"; // Default file
  
  if (type == Folder) {
      unicodeStr = "\uf07b";
      if (colorize) iconColor = QColor("#89b4fa");
  }

  painter.setPen(iconColor);
  painter.drawText(0, 0, 16, 16, Qt::AlignCenter, unicodeStr);
  return QIcon(pixmap);
}

} // namespace gridlock::ui
