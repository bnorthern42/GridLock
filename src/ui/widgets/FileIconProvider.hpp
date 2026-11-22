#pragma once

/**
 * @file FileIconProvider.hpp
 * @brief High-DPI theme-aware vector file and semantic folder icon provider.
 *
 * Implements FileIconProvider, deriving from QAbstractFileIconProvider to supply
 * crisp @2x QPainter-rendered vector icons for C/C++ sources, headers, shell scripts,
 * build files, Python, Fortran, CUDA, Assembly, data/config files, documentation,
 * compiled object artifacts, and semantic project directories with zero external font dependency.
 */

#include <QAbstractFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QString>

namespace gridlock::ui {

/**
 * @class FileIconProvider
 * @brief Cached high-DPI vector icon provider mapping file types and semantic folders to crisp badges.
 */
class FileIconProvider : public QAbstractFileIconProvider {
public:
    FileIconProvider();
    ~FileIconProvider() override = default;

    /** @brief Returns a cached high-DPI vector icon for @p info. */
    QIcon icon(const QFileInfo &info) const override;

    /** @brief Returns a cached high-DPI vector icon for standard abstract @p type. */
    QIcon icon(IconType type) const override;

    /** @brief Returns a stable semantic icon key (e.g. "cpp", "hpp", "sh", "build", "dir_src"). */
    static QString iconKeyForFile(const QFileInfo &info);

    /** @brief Returns true when @p info is a build/object/dependency artifact (.o, .d, .lo, .a). */
    static bool isBuildArtifactFile(const QFileInfo &info);

private:
    QIcon renderIconForKey(const QString &key, bool colorize) const;
    mutable QHash<QString, QIcon> m_cache;
};

}  // namespace gridlock::ui
