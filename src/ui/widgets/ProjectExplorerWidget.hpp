#pragma once

/**
 * @file ProjectExplorerWidget.hpp
 * @brief Multi-mode project explorer, symbol table, and binary strings inspector.
 *
 * Implements ProjectExplorerWidget, housing a tri-mode docked explorer (FILES | SYMBOLS | STRINGS)
 * with file browsing, symbol searching, string extraction, and synchronized address seeking.
 */

#include <QDockWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QIdentityProxyModel>
#include <QPushButton>
#include <QToolButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <cstdint>
#include <vector>

namespace gridlock::ui {

class FileIconProvider;

/**
 * @struct SymbolEntry
 * @brief Representation of an ELF/Mach-O/PE exported or debug symbol.
 */
struct SymbolEntry {
    QString name;
    uint64_t address = 0;
    uint32_t size = 0;
};

/**
 * @struct StringEntry
 * @brief Representation of an extracted ASCII or UTF-8 binary string.
 */
struct StringEntry {
    uint64_t address = 0;
    QString section;
    QString value;
};

/**
 * @class ProjectExplorerWidget
 * @brief Multi-mode dockable sidebar providing Files, Symbols, and Strings views.
 */
class ProjectExplorerWidget : public QDockWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new ProjectExplorerWidget.
     * @param parent Optional parent widget.
     */
    explicit ProjectExplorerWidget(QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~ProjectExplorerWidget() override;
    
    /**
     * @brief Sets root directory path displayed in the file tree.
     * @param path Directory path string.
     */
    void setRootPath(const QString& path);

    /**
     * @brief Reloads fonts and palette settings.
     */
    void reloadStyle();

    /**
     * @brief Toggles between Files view and Symbols table view.
     * @param enabled True for Symbols mode, false for Files mode.
     */
    void setSymbolsMode(bool enabled);

    /**
     * @brief Checks if the widget is currently displaying the Symbols view.
     * @return True if in Symbols mode.
     */
    bool isSymbolsMode() const;

    /**
     * @brief Returns total number of parsed symbols currently loaded.
     * @return Count of loaded symbols.
     */
    int symbolCount() const;

    /**
     * @brief Programmatically selects and activates a symbol by name.
     * @param symbolName Exact or partial symbol name.
     * @return True if symbol found and activated.
     */
    bool activateSymbol(const QString& symbolName);

    /**
     * @brief Populates the symbols view with externally parsed symbols.
     * @param symbols Vector of symbol entries.
     */
    void setSymbols(const std::vector<SymbolEntry>& symbols);

    /**
     * @brief Populates the strings view with externally parsed strings.
     * @param strings Vector of string entries.
     */
    void setStrings(const std::vector<StringEntry>& strings);

    /**
     * @brief Toggles the binary strings inspector mode.
     * @param enabled True for Strings mode.
     */
    void setStringsMode(bool enabled);

    /**
     * @brief Checks if the widget is currently displaying the Strings view.
     * @return True if in Strings mode.
     */
    bool isStringsMode() const;

    /**
     * @brief Returns total number of extracted strings loaded.
     * @return String count.
     */
    int stringCount() const;

    /**
     * @brief Programmatically selects and activates a string by query or address.
     * @param query String substring or hex address.
     * @return True if match found and activated.
     */
    bool activateString(const QString& query);

    /**
     * @brief Creates a new source or header file in parentDir with a standard boilerplate stub.
     * @param parentDir Target directory path on disk.
     * @param fileName File name including extension (e.g., "solver_kernel.cpp").
     * @return True if the file was created on disk and fileCreated(path) was emitted.
     */
    bool createNewFile(const QString& parentDir, const QString& fileName);

    /**
     * @brief Creates a new subdirectory inside parentDir and refreshes the file tree.
     * @param parentDir Parent directory path on disk.
     * @param folderName Subdirectory name to create.
     * @return True if the directory was created on disk.
     */
    bool createNewFolder(const QString& parentDir, const QString& folderName);

    /**
     * @brief Renames an existing file or folder on disk and emits fileRenamed(oldPath, newPath).
     * @param oldPath Absolute path of the existing file or folder.
     * @param newName New base file/folder name within the same parent directory.
     * @return True if the rename succeeded on disk.
     */
    bool renameEntry(const QString& oldPath, const QString& newName);

    /**
     * @brief Deletes an existing file or folder from disk and emits fileDeleted(path).
     * @param targetPath Absolute path of the file or folder to remove.
     * @return True if the entry was removed from disk.
     */
    bool deleteEntry(const QString& targetPath);

    /**
     * @brief Registers a relative source file path in a Meson (meson.build) or CMake (CMakeLists.txt) build file.
     * @param buildFilePath Absolute path to meson.build or CMakeLists.txt.
     * @param relativeSourcePath Relative path of the C/C++ source file to add.
     * @return True if the source file was registered in the build specification.
     */
    bool registerSourceInBuildFile(const QString& buildFilePath, const QString& relativeSourcePath);

signals:
    /**
     * @brief Emitted when a file is double-clicked in the tree view.
     * @param filePath Absolute path of the selected file.
     */
    void fileDoubleClicked(const QString& filePath);

    /**
     * @brief Emitted when a symbol or string entry is selected to seek that memory address.
     * @param address 64-bit target address.
     * @param symbolName Associated symbol name or string description.
     */
    void symbolAddressSelected(uint64_t address, const QString& symbolName);

    /**
     * @brief Emitted when a new source or header file is created via the explorer.
     * @param path Absolute path to the newly created file.
     */
    void fileCreated(const QString& path);

    /**
     * @brief Emitted when a file or folder is renamed via the explorer.
     * @param oldPath Previous absolute path.
     * @param newPath Updated absolute path.
     */
    void fileRenamed(const QString& oldPath, const QString& newPath);

    /**
     * @brief Emitted when a file or folder is deleted via the explorer.
     * @param path Absolute path of the deleted entry.
     */
    void fileDeleted(const QString& path);

private slots:
    void onOpenFolder();
    void onDoubleClicked(const QModelIndex& index);
    void onTreeCustomContextMenu(const QPoint& pos);
    void filterSymbols(const QString& filterText);
    void filterStrings(const QString& filterText);

private:
    void seedDefaultSymbols();
    void populateSymbolsTable(const QString& filterText = QString());
    void seedDefaultStrings();
    void populateStringsTable(const QString& filterText = QString());
    void refreshTreeViewPath(const QString& focusPath = QString());

    QTreeView* m_treeView = nullptr;
    QFileSystemModel* m_fileSystemModel = nullptr;
    FileIconProvider* m_iconProvider = nullptr;
    QPushButton* m_openFolderBtn = nullptr;
    QPushButton* m_collapseAllBtn = nullptr;
    QPushButton* m_filesModeBtn = nullptr;
    QPushButton* m_symbolsModeBtn = nullptr;
    QPushButton* m_stringsModeBtn = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLineEdit* m_symbolFilterEdit = nullptr;
    QTableWidget* m_symbolsTable = nullptr;
    QLineEdit* m_stringFilterEdit = nullptr;
    QTableWidget* m_stringsTable = nullptr;
    std::vector<SymbolEntry> m_symbols;
    std::vector<StringEntry> m_strings;
};

}  // namespace gridlock::ui

