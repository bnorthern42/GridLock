#pragma once
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

struct SymbolEntry {
    QString name;
    uint64_t address = 0;
    uint32_t size = 0;
};

struct StringEntry {
    uint64_t address = 0;
    QString section;
    QString value;
};

class ProjectExplorerWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit ProjectExplorerWidget(QWidget* parent = nullptr);
    ~ProjectExplorerWidget() override;
    
    void setRootPath(const QString& path);
    void reloadStyle();

    void setSymbolsMode(bool enabled);
    bool isSymbolsMode() const;
    int symbolCount() const;
    bool activateSymbol(const QString& symbolName);

    void setStringsMode(bool enabled);
    bool isStringsMode() const;
    int stringCount() const;
    bool activateString(const QString& query);

signals:
    void fileDoubleClicked(const QString& filePath);
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

} // namespace gridlock::ui
