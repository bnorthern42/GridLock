/**
 * @file ProjectWizard.hpp
 * @brief Multi-step wizard dialog for creating and initializing new MPI projects.
 *
 * Guides users through project workspace folder selection, binary executable paths,
 * runtime arguments, MPI rank counts, CPU core binding rules, and OpenMPI flags.
 */

#pragma once

#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>
#include <QWizard>
#include <QWizardPage>

namespace gridlock::ui::dialogs {

/**
 * @class ProjectWizard
 * @brief Multi-page wizard dialog configuring MPI project creation.
 */
class ProjectWizard : public QWizard {
  Q_OBJECT

public:
  /**
   * @brief Constructs wizard pages and initializes default MPI launcher settings.
   * @param parent Optional parent widget.
   */
  explicit ProjectWizard(QWidget *parent = nullptr);

  /**
   * @brief Default virtual destructor.
   */
  ~ProjectWizard() override = default;

  /**
   * @brief Constructs the synthesized `mpirun` command string based on wizard selections.
   * @return Shell command line string.
   */
  QString getMpiLaunchCommand() const;

  /**
   * @brief Retrieves the chosen project root workspace directory.
   * @return Filesystem directory path.
   */
  QString getProjectRoot() const;

  /**
   * @brief Retrieves the target executable binary path.
   * @return Absolute binary path.
   */
  QString getTargetBinary() const;

  /**
   * @brief Retrieves user-specified target command-line arguments.
   * @return Argument string.
   */
  QString getExecutableArguments() const;

  /**
   * @brief Retrieves the number of MPI ranks configured in the wizard.
   * @return Integer rank count.
   */
  int getRanks() const;

private:
  /**
   * @brief Constructs Page 1: Workspace root, project name, target executable, and args.
   * @return Configured wizard page instance.
   */
  QWizardPage *createWorkspacePage();

  /**
   * @brief Constructs Page 2: MPI ranks, CPU binding, oversubscription, and MCA flags.
   * @return Configured wizard page instance.
   */
  QWizardPage *createMpiPage();

  // Page 1 Controls: Workspace & Executable
  QLineEdit *m_projectNameEdit{nullptr};    ///< Name label for new project.
  QLineEdit *m_workspaceDirEdit{nullptr};   ///< Target root directory path.
  QLineEdit *m_targetExecEdit{nullptr};     ///< Target binary path.
  QLineEdit *m_execArgsEdit{nullptr};       ///< Program execution arguments.

  // Page 2 Controls: MPI Topology & Flags
  QSpinBox *m_ranksBox{nullptr};                    ///< Number of MPI ranks (-np).
  QCheckBox *m_bindToCoreCheck{nullptr};            ///< Bind ranks to CPU cores.
  QCheckBox *m_mapByNodeCheck{nullptr};             ///< Map ranks round-robin by node.
  QCheckBox *m_useHwThreadsCheck{nullptr};          ///< Treat hardware hyperthreads as independent CPUs.
  QCheckBox *m_oversubscribeCheck{nullptr};         ///< Allow rank count to exceed physical slots.
  QCheckBox *m_reportBindingsCheck{nullptr};        ///< Report rank-to-core CPU bindings.
  QCheckBox *m_displayMapCheck{nullptr};            ///< Display process allocation map.
  QCheckBox *m_tagOutputCheck{nullptr};             ///< Prefix stdout lines with rank IDs.
  QCheckBox *m_timestampOutputCheck{nullptr};       ///< Prefix stdout lines with timestamps.
  QLineEdit *m_customMpiArgsEdit{nullptr};          ///< Freeform extra MPI arguments.
};

}  // namespace gridlock::ui::dialogs
