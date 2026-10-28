/**
 * @file TutorialDialog.hpp
 * @brief Interactive tutorial launcher and sample project loader dialog.
 *
 * Provides a selector dialog presenting interactive demo projects (Deadlock Demo,
 * Inspection Demo, Register Demo, MemView Demo) with automatic sample compilation
 * and predefined breakpoint injection.
 */

#pragma once

#include <QDialog>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QString>

namespace gridlock::ui {

/**
 * @class TutorialDialog
 * @brief Dialog presenting catalog of interactive MPI tutorials.
 */
class TutorialDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs the tutorial dialog and populates demo entries.
     * @param parent Optional parent widget.
     */
    explicit TutorialDialog(QWidget* parent = nullptr);

    /**
     * @brief Looks up default breakpoint line set for a tutorial source file.
     * @param filePath Relative or absolute path to sample source file.
     * @return Set of 1-based source line numbers.
     */
    static QSet<int> getBreakpointsForFile(const QString& filePath);

    /**
     * @brief Injects tutorial breakpoints into ConfigManager storage.
     * @param relativePath Relative sample source path.
     * @param absPath Absolute target source file path.
     */
    static void injectBreakpoints(const QString& relativePath, const QString& absPath);

signals:
    /**
     * @brief Emitted when user clicks Launch Tutorial on an active sample page.
     * @param absoluteFilePath Absolute path to selected sample C/C++ source file.
     */
    void launchTutorialRequested(const QString& absoluteFilePath);

private slots:
    /**
     * @brief Synchronizes stacked preview widget with list selection.
     * @param currentRow Index of selected row in QListWidget.
     */
    void onSelectionChanged(int currentRow);

    /**
     * @brief Validates file existence, injects breakpoints, and emits launch request.
     */
    void onLaunchClicked();

private:
    /**
     * @brief Populates tutorial names and description cards.
     */
    void populateList();

    QListWidget* m_listWidget{nullptr};          ///< List displaying tutorial titles.
    QStackedWidget* m_stackedWidget{nullptr};    ///< Container hosting detail description cards.
};

}  // namespace gridlock::ui
