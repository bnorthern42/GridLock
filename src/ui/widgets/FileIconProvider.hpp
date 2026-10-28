#pragma once

/**
 * @file FileIconProvider.hpp
 * @brief High-DPI theme-aware file system icon provider.
 *
 * Implements FileIconProvider, deriving from QAbstractFileIconProvider to supply
 * custom SVG and vector icons for files, folders, C++ sources, headers, build configs,
 * and binary executables in ProjectExplorerWidget.
 */

#include <QAbstractFileIconProvider>
#include <QFileInfo>
#include <QIcon>

namespace gridlock::ui {

/**
 * @class FileIconProvider
 * @brief Icon provider subclass mapping file extensions to high-DPI icons.
 */
class FileIconProvider : public QAbstractFileIconProvider {
public:
    /**
     * @brief Constructs a new FileIconProvider.
     */
    FileIconProvider();

    /**
     * @brief Destructor.
     */
    ~FileIconProvider() override = default;

    /**
     * @brief Returns an appropriate icon for a given file info.
     * @param info QFileInfo describing the target file or directory.
     * @return QIcon matching the file type or extension.
     */
    QIcon icon(const QFileInfo &info) const override;

    /**
     * @brief Returns an icon for a standard abstract icon type.
     * @param type Abstract IconType enum value.
     * @return QIcon for the type.
     */
    QIcon icon(IconType type) const override;
};

}  // namespace gridlock::ui

