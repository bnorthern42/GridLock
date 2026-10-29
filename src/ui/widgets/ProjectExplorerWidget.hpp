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

private slots:
    void onOpenFolder();
    void onDoubleClicked(const QModelIndex& index);
    void filterSymbols(const QString& filterText);
    void filterStrings(const QString& filterText);

private:
    void seedDefaultSymbols();
    void populateSymbolsTable(const QString& filterText = QString());
    void seedDefaultStrings();
    void populateStringsTable(const QString& filterText = QString());

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

