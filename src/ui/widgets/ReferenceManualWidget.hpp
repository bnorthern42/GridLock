#pragma once

/**
 * @file ReferenceManualWidget.hpp
 * @brief Embedded reference manual and man page documentation browser.
 *
 * Implements ReferenceManualWidget, DocsetViewerWidget, and ManPageViewerWidget,
 * providing offline API reference documentation, Dash/Zeal docset querying,
 * and Linux system man page rendering.
 */

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTextBrowser>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class DocsetViewerWidget
 * @brief Searchable viewer for offline HTML API docsets (MPI, C++, POSIX).
 */
class DocsetViewerWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new DocsetViewerWidget.
     * @param parent Optional parent widget.
     */
    explicit DocsetViewerWidget(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~DocsetViewerWidget() override;

    /**
     * @brief Looks up and renders formatted HTML documentation for the specified symbol.
     * @param rawSymbol Symbol identifier (e.g., "MPI_Init@plt" or "MPI_Send").
     * @return True if documentation was resolved and rendered; false if symbol is empty.
     */
    bool showSymbolDocumentation(const QString& rawSymbol);

    /**
     * @brief Re-discovers installed Zeal docsets and refreshes the docset selector combo box.
     */
    void refreshDocsets();

    /**
     * @brief Returns the list of loaded Zeal docset names (or built-in fallback names).
     * @return List of active docset names.
     */
    QStringList loadedDocsetNames() const;

    /**
     * @brief Returns the docset bundle selector combo box so it can be hosted in the unified header.
     * @return Pointer to the QComboBox selector.
     */
    QComboBox* docsetSelector() const { return m_docsetSelector; }

    /**
     * @brief Returns the symbol search input box so it can be hosted in the unified header.
     * @return Pointer to the QLineEdit search input.
     */
    QLineEdit* searchEdit() const { return m_searchEdit; }

private slots:
    /**
     * @brief Executes keyword search across available docset indices.
     */
    void performSearch();

    /**
     * @brief Handles result item selection and renders the corresponding HTML document.
     * @param row Index of selected result.
     */
    void onResultClicked(int row);

private:
    QComboBox* m_docsetSelector = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_resultsList = nullptr;
    QTextBrowser* m_textBrowser = nullptr;
    
    /// Pairs of <Keyword, AbsoluteHTMLPath> for easy access
    QList<QPair<QString, QString>> m_searchResults;
};

/**
 * @class ManPageViewerWidget
 * @brief Interactive UNIX man page searcher and renderer using system `man` or `groff`.
 */
class ManPageViewerWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new ManPageViewerWidget.
     * @param parent Optional parent widget.
     */
    explicit ManPageViewerWidget(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~ManPageViewerWidget() override;

    /**
     * @brief Returns the man page search input box so it can be hosted in the unified header.
     * @return Pointer to the QLineEdit search input.
     */
    QLineEdit* searchEdit() const { return m_searchEdit; }

private slots:
    /**
     * @brief Spawns asynchronous `man` process to retrieve manual page text.
     */
    void performSearch();

    /**
     * @brief Handles completion of the background `man` process.
     * @param exitCode Subprocess exit code.
     * @param exitStatus Subprocess exit status.
     */
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QLineEdit* m_searchEdit = nullptr;
    QTextBrowser* m_textBrowser = nullptr;
    QProcess* m_process = nullptr;
};

/**
 * @class ReferenceManualWidget
 * @brief Unified single-header container housing docset API references and system manual pages.
 */
class ReferenceManualWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new ReferenceManualWidget with a single 26px control strip.
     * @param parent Optional parent widget.
     */
    explicit ReferenceManualWidget(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~ReferenceManualWidget() override;

    /**
     * @brief Resolves and displays symbol documentation in the active Docset browser.
     * @param rawSymbol Raw symbol or topic name (strips @plt/@got suffixes automatically).
     * @return True if documentation was rendered; false if symbol is empty.
     */
    bool lookupSymbol(const QString& rawSymbol);

    /**
     * @brief Returns the list of loaded Zeal docset bundle names available in the viewer.
     * @return Non-empty list of discovered Zeal docsets (plus Built-in MPI/ABI).
     */
    QStringList loadedDocsetNames() const;

    /**
     * @brief Returns the pixel height of the consolidated top header bar (<= 26px).
     * @return Header bar height in pixels.
     */
    int headerBarHeight() const;

private:
    QWidget* m_headerBar = nullptr;
    QPushButton* m_docsetsModeBtn = nullptr;
    QPushButton* m_manPagesModeBtn = nullptr;
    QComboBox* m_quickTopicCombo = nullptr;
    QStackedWidget* m_stackedWidget = nullptr;
    DocsetViewerWidget* m_docsetViewer = nullptr;
    ManPageViewerWidget* m_manPageViewer = nullptr;
};

}  // namespace gridlock::ui


