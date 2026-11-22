/**
 * @file FileIconProvider.cpp
 * @brief High-DPI theme-aware vector file and semantic folder icon provider implementation.
 *
 * Renders crisp @2x (32x32 physical -> 16x16 logical) QPainter vector badges cached
 * by semantic key so every file type (.cpp vs .hpp, .sh, meson.build/CMakeLists.txt,
 * .py, .f90/.cu/.asm, .json/.toml/.yaml, .md, .o/.d) and semantic folder (src, tests,
 * scripts, docs, build) is immediately distinguishable with zero external font dependency.
 */

#include "FileIconProvider.hpp"

#include "../../core/managers/ConfigManager.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>

namespace gridlock::ui {

namespace {

bool isColorizeEnabled() {
    QSettings s(gridlock::core::ConfigManager::debuggerSettingsPath(), QSettings::IniFormat);
    if (s.contains(QStringLiteral("appearance/colorize_icons"))) {
        return s.value(QStringLiteral("appearance/colorize_icons"), true).toBool();
    }
    QSettings legacy(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    return legacy.value(QStringLiteral("appearance/colorize_icons"), true).toBool();
}

void drawFolderVector(QPainter &p, const QColor &fill, const QColor &accent, const QString &mark) {
    QPainterPath back;
    back.moveTo(1.5, 4.0);
    back.lineTo(1.5, 3.2);
    back.quadTo(1.5, 2.2, 2.5, 2.2);
    back.lineTo(6.2, 2.2);
    back.lineTo(7.6, 3.8);
    back.lineTo(13.5, 3.8);
    back.quadTo(14.5, 3.8, 14.5, 4.8);
    back.lineTo(14.5, 12.6);
    back.quadTo(14.5, 13.6, 13.5, 13.6);
    back.lineTo(2.5, 13.6);
    back.quadTo(1.5, 13.6, 1.5, 12.6);
    back.closeSubpath();

    p.setPen(QPen(fill.darker(125), 0.9));
    p.setBrush(fill);
    p.drawPath(back);

    // Subtle front folder lip highlight.
    p.setPen(Qt::NoPen);
    p.setBrush(fill.lighter(118));
    p.drawRoundedRect(QRectF(2.0, 5.2, 12.0, 7.8), 1.4, 1.4);

    if (!mark.isEmpty()) {
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(7.2, 7.6, 6.2, 5.2), 1.2, 1.2);
        QFont f(QStringLiteral("Sans Serif"));
        f.setPixelSize(5);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(7.2, 7.4, 6.2, 5.4), Qt::AlignCenter, mark);
    }
}

void drawBadgeVector(QPainter &p, const QColor &bg, const QColor &fg, const QString &label, int pixelSize = 7) {
    p.setPen(QPen(bg.darker(130), 0.9));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(1.2, 1.8, 13.6, 12.4), 2.8, 2.8);

    QFont f(QStringLiteral("Sans Serif"));
    f.setPixelSize(pixelSize);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(fg);
    p.drawText(QRectF(1.2, 1.8, 13.6, 12.4), Qt::AlignCenter, label);
}

void drawDocVector(QPainter &p, const QColor &accent, bool isMarkdown) {
    QPainterPath doc;
    doc.moveTo(2.5, 1.5);
    doc.lineTo(10.0, 1.5);
    doc.lineTo(13.5, 5.0);
    doc.lineTo(13.5, 14.5);
    doc.lineTo(2.5, 14.5);
    doc.closeSubpath();

    p.setPen(QPen(accent.darker(125), 1.0));
    p.setBrush(accent.lighter(165));
    p.drawPath(doc);

    // Folded corner dog-ear.
    QPainterPath fold;
    fold.moveTo(10.0, 1.5);
    fold.lineTo(10.0, 5.0);
    fold.lineTo(13.5, 5.0);
    fold.closeSubpath();
    p.setBrush(accent);
    p.drawPath(fold);

    if (isMarkdown) {
        QFont f(QStringLiteral("Sans Serif"));
        f.setPixelSize(7);
        f.setBold(true);
        p.setFont(f);
        p.setPen(accent.darker(150));
        p.drawText(QRectF(2.5, 5.6, 11.0, 8.2), Qt::AlignCenter, QStringLiteral("MD"));
    } else {
        p.setPen(QPen(accent.darker(130), 1.0));
        p.drawLine(QPointF(4.4, 7.6), QPointF(11.4, 7.6));
        p.drawLine(QPointF(4.4, 10.0), QPointF(11.4, 10.0));
        p.drawLine(QPointF(4.4, 12.4), QPointF(9.2, 12.4));
    }
}

void drawBuildVector(QPainter &p, const QColor &bg, const QColor &fg) {
    p.setPen(QPen(bg.darker(130), 0.9));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(1.2, 1.8, 13.6, 12.4), 2.8, 2.8);

    // Crisp build triangle/prism icon inside amber badge.
    QPainterPath tri;
    tri.moveTo(8.0, 3.8);
    tri.lineTo(12.2, 11.8);
    tri.lineTo(3.8, 11.8);
    tri.closeSubpath();
    p.setPen(QPen(fg, 1.1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(tri);
    p.setBrush(fg);
    p.drawEllipse(QPointF(8.0, 9.2), 1.3, 1.3);
}

}  // namespace

FileIconProvider::FileIconProvider() : QAbstractFileIconProvider() {}

bool FileIconProvider::isBuildArtifactFile(const QFileInfo &info) {
    if (info.isDir()) return false;
    const QString ext = info.suffix().toLower();
    return ext == QLatin1String("o") || ext == QLatin1String("d") ||
           ext == QLatin1String("lo") || ext == QLatin1String("a") ||
           ext == QLatin1String("obj") || ext == QLatin1String("dep") ||
           ext == QLatin1String("pyc");
}

QString FileIconProvider::iconKeyForFile(const QFileInfo &info) {
    const QString name = info.fileName().toLower();
    if (info.isDir()) {
        if (name == QLatin1String("src") || name == QLatin1String("source") || name == QLatin1String("include"))
            return QStringLiteral("dir_src");
        if (name == QLatin1String("tests") || name == QLatin1String("test"))
            return QStringLiteral("dir_tests");
        if (name == QLatin1String("scripts") || name == QLatin1String("bin"))
            return QStringLiteral("dir_scripts");
        if (name == QLatin1String("docs") || name == QLatin1String("doc") || name == QLatin1String("tutorial"))
            return QStringLiteral("dir_docs");
        if (name.startsWith(QLatin1String("build")) || name == QLatin1String("out") || name == QLatin1String("dist"))
            return QStringLiteral("dir_build");
        return QStringLiteral("dir");
    }

    const QString ext = info.suffix().toLower();
    if (name == QLatin1String("meson.build") || name == QLatin1String("meson_options.txt") ||
        name == QLatin1String("cmakelists.txt") || name == QLatin1String("makefile") ||
        name == QLatin1String("gnumakefile") || name == QLatin1String("build.ninja") ||
        ext == QLatin1String("cmake") || ext == QLatin1String("mk")) {
        return QStringLiteral("build");
    }
    if (ext == QLatin1String("cpp") || ext == QLatin1String("cc") || ext == QLatin1String("cxx"))
        return QStringLiteral("cpp");
    if (ext == QLatin1String("hpp") || ext == QLatin1String("h") || ext == QLatin1String("hh") ||
        ext == QLatin1String("hxx") || ext == QLatin1String("inl"))
        return QStringLiteral("hpp");
    if (ext == QLatin1String("c")) return QStringLiteral("c");
    if (ext == QLatin1String("sh") || ext == QLatin1String("bash") || ext == QLatin1String("zsh"))
        return QStringLiteral("sh");
    if (ext == QLatin1String("py") || ext == QLatin1String("pyi")) return QStringLiteral("py");
    if (ext == QLatin1String("f90") || ext == QLatin1String("f95") || ext == QLatin1String("f03") ||
        ext == QLatin1String("f") || ext == QLatin1String("for"))
        return QStringLiteral("fortran");
    if (ext == QLatin1String("cu") || ext == QLatin1String("cuh")) return QStringLiteral("cuda");
    if (ext == QLatin1String("s") || ext == QLatin1String("asm")) return QStringLiteral("asm");
    if (ext == QLatin1String("json") || ext == QLatin1String("toml") || ext == QLatin1String("yaml") ||
        ext == QLatin1String("yml") || ext == QLatin1String("xml") || ext == QLatin1String("ini") ||
        ext == QLatin1String("conf") || ext == QLatin1String("cfg"))
        return QStringLiteral("config");
    if (ext == QLatin1String("md") || ext == QLatin1String("rst")) return QStringLiteral("md");
    if (ext == QLatin1String("txt")) return QStringLiteral("doc");
    if (isBuildArtifactFile(info) || ext == QLatin1String("so") || ext == QLatin1String("elf") ||
        (ext.isEmpty() && info.isExecutable()))
        return QStringLiteral("artifact");
    return QStringLiteral("file");
}

QIcon FileIconProvider::renderIconForKey(const QString &key, bool colorize) const {
    const QString cacheKey = (colorize ? QStringLiteral("c:") : QStringLiteral("m:")) + key;
    if (auto it = m_cache.constFind(cacheKey); it != m_cache.constEnd()) return it.value();

    QPixmap pix(32, 32);
    pix.setDevicePixelRatio(2.0);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor monoBg(QStringLiteral("#64748b"));
    const QColor monoDir(QStringLiteral("#7c8aa0"));
    auto col = [&](const char *hex) { return colorize ? QColor(QString::fromLatin1(hex)) : monoBg; };
    auto dcol = [&](const char *hex) { return colorize ? QColor(QString::fromLatin1(hex)) : monoDir; };

    if (key == QLatin1String("dir_src")) drawFolderVector(p, dcol("#38bdf8"), col("#0369a1"), QStringLiteral("S"));
    else if (key == QLatin1String("dir_tests")) drawFolderVector(p, dcol("#34d399"), col("#047857"), QStringLiteral("T"));
    else if (key == QLatin1String("dir_scripts")) drawFolderVector(p, dcol("#2dd4bf"), col("#0f766e"), QStringLiteral(">"));
    else if (key == QLatin1String("dir_docs")) drawFolderVector(p, dcol("#a78bfa"), col("#6d28d9"), QStringLiteral("D"));
    else if (key == QLatin1String("dir_build")) drawFolderVector(p, dcol("#64748b"), col("#334155"), QStringLiteral("B"));
    else if (key == QLatin1String("dir")) drawFolderVector(p, dcol("#60a5fa"), col("#1d4ed8"), QString());
    else if (key == QLatin1String("cpp")) drawBadgeVector(p, col("#0284c7"), Qt::white, QStringLiteral("C++"), 6);
    else if (key == QLatin1String("hpp")) drawBadgeVector(p, col("#7c3aed"), Qt::white, QStringLiteral("H"), 8);
    else if (key == QLatin1String("c")) drawBadgeVector(p, col("#2563eb"), Qt::white, QStringLiteral("C"), 8);
    else if (key == QLatin1String("sh")) drawBadgeVector(p, col("#059669"), Qt::white, QStringLiteral(">_"), 7);
    else if (key == QLatin1String("build")) drawBuildVector(p, col("#d97706"), Qt::white);
    else if (key == QLatin1String("py")) drawBadgeVector(p, col("#ca8a04"), Qt::white, QStringLiteral("PY"), 7);
    else if (key == QLatin1String("fortran")) drawBadgeVector(p, col("#db2777"), Qt::white, QStringLiteral("F90"), 5);
    else if (key == QLatin1String("cuda")) drawBadgeVector(p, col("#65a30d"), Qt::white, QStringLiteral("CU"), 7);
    else if (key == QLatin1String("asm")) drawBadgeVector(p, col("#dc2626"), Qt::white, QStringLiteral("ASM"), 5);
    else if (key == QLatin1String("config")) drawBadgeVector(p, col("#0d9488"), Qt::white, QStringLiteral("{}"), 7);
    else if (key == QLatin1String("md")) drawDocVector(p, col("#6366f1"), true);
    else if (key == QLatin1String("doc")) drawDocVector(p, col("#64748b"), false);
    else if (key == QLatin1String("artifact")) drawBadgeVector(p, col("#475569"), QColor(QStringLiteral("#cbd5e1")), QStringLiteral("01"), 7);
    else drawDocVector(p, col("#94a3b8"), false);

    p.end();
    QIcon icon(pix);
    m_cache.insert(cacheKey, icon);
    return icon;
}

QIcon FileIconProvider::icon(const QFileInfo &info) const {
    return renderIconForKey(iconKeyForFile(info), isColorizeEnabled());
}

QIcon FileIconProvider::icon(IconType type) const {
    return renderIconForKey(type == Folder ? QStringLiteral("dir") : QStringLiteral("file"), isColorizeEnabled());
}

}  // namespace gridlock::ui
