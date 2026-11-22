/**
 * @file MainWindowBuild.cpp
 * @brief Target discovery, build-system pre-flight compilation, workspace switching, and process attachment.
 *
 * Implements tutorial/workspace path resolution, ELF executable scanning, on-demand `-g -O0`
 * compilation, Meson/CMake/Make/SingleFileMPI build/clean/rebuild orchestration, workspace
 * root switching, and local/remote process attachment workflows.
 */

#include "MainWindow.hpp"

#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/utils/ClangdLspClient.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/AttachProcessDialog.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "utils/FileDialogHelper.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>

namespace gridlock::ui {

/** @brief Resolves a tutorial target name or relative path across local and AppImage share directories. */
QString resolveTutorialTarget(const QString& target) {
    QString rel = target.trimmed();
    if (rel.isEmpty()) return QString();
    if (!rel.endsWith(QStringLiteral(".cpp")) && !rel.contains(QLatin1Char('/'))) {
        rel = QStringLiteral("tutorial/") + rel + QStringLiteral(".cpp");
    } else if (!rel.startsWith(QStringLiteral("tutorial/")) && !rel.startsWith(QLatin1Char('/'))) {
        rel = QStringLiteral("tutorial/") + rel;
    }
    auto canonOrAbs = [](const QString& p) {
        const QFileInfo fi(p);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    };
    if (QFileInfo::exists(rel)) return canonOrAbs(rel);
    if (const QString parentCand = QStringLiteral("../") + rel; QFileInfo::exists(parentCand)) return canonOrAbs(parentCand);
    const QString envAppDir = qEnvironmentVariable("APPDIR");
    if (!envAppDir.isEmpty()) {
        if (const QString appDirShare = envAppDir + QStringLiteral("/usr/share/gridlock/") + rel; QFileInfo::exists(appDirShare)) return canonOrAbs(appDirShare);
        if (const QString appDirDirect = envAppDir + QStringLiteral("/") + rel; QFileInfo::exists(appDirDirect)) return canonOrAbs(appDirDirect);
    }
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QString shareCand = appDirPath + QStringLiteral("/../share/gridlock/") + rel;
    if (QFileInfo::exists(shareCand)) return canonOrAbs(shareCand);
    if (const QString localCand = appDirPath + QStringLiteral("/") + rel; QFileInfo::exists(localCand)) return canonOrAbs(localCand);
    return !envAppDir.isEmpty() ? (envAppDir + QStringLiteral("/usr/share/gridlock/") + rel) : shareCand;
}

/** @brief Ensures a writable execution path for tutorial targets in read-only AppImage environments. */
QString ensureWritableTutorialBinaryPath(const QString& sourcePath) {
    if (sourcePath.isEmpty()) return QString();
    const QFileInfo srcInfo(sourcePath);
    const QString baseName = srcInfo.completeBaseName(), companionBin = srcInfo.dir().filePath(baseName);
    if (!srcInfo.dir().exists() || !QFileInfo(srcInfo.dir().absolutePath()).isWritable()) {
        const QString cacheDir = QStringLiteral("/tmp/gridlock_tutorials");
        QDir().mkpath(cacheDir);
        const QString targetBin = cacheDir + QStringLiteral("/") + baseName;
        if (QFileInfo::exists(companionBin) &&
            (!QFileInfo::exists(targetBin) || QFileInfo(targetBin).lastModified() < QFileInfo(companionBin).lastModified())) {
            QFile::remove(targetBin);
            QFile::copy(companionBin, targetBin);
            QFile::setPermissions(targetBin, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
        }
        return targetBin;
    }
    return companionBin;
}

/** @brief Resolves a relative or absolute workspace file path across current and parent directories. */
QString MainWindow::resolveWorkspacePath(const QString& rawPath) {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w); mw && !mw->findChild<QAction*>(QStringLiteral("actionSignalsAndCatchpoints"))) {
            mw->rebuildDynamicSourceLineMap();
        }
    }
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) return QString();
    if (QFileInfo::exists(trimmed)) {
        const QFileInfo fi(trimmed);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }
    const QString parentCandidate = QStringLiteral("../") + trimmed;
    if (QFileInfo::exists(parentCandidate)) {
        const QFileInfo fi(parentCandidate);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }
    return QFileInfo(trimmed).absoluteFilePath();
}

/** @brief Checks whether @p filePath is a runnable 64/32-bit ELF `ET_EXEC`/`ET_DYN` binary. */
bool isRunnableElfExecutable(const QString& filePath) {
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile() || !fi.isExecutable() || fi.size() < 64) return false;
    const QString abs = fi.absoluteFilePath(), name = fi.fileName();
    if (abs.contains(QStringLiteral("/CMakeFiles/")) || abs.contains(QStringLiteral("/meson-private/")) ||
        abs.contains(QStringLiteral(".p/")) || abs.contains(QStringLiteral(".dir/"))) return false;
    if (name.endsWith(QStringLiteral(".so")) || name.contains(QStringLiteral(".so.")) ||
        name.endsWith(QStringLiteral(".o")) || name.endsWith(QStringLiteral(".a")) ||
        name.endsWith(QStringLiteral(".lo")) || name.contains(QStringLiteral("sanitycheck"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("CompilerId"), Qt::CaseInsensitive)) return false;
    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray hdr = f.read(20);
    if (hdr.size() < 20 || hdr[0] != '\x7f' || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') return false;
    const auto eType = static_cast<uint16_t>(static_cast<uint8_t>(hdr[16]) | (static_cast<uint16_t>(static_cast<uint8_t>(hdr[17])) << 8U));
    return eType == 2U || eType == 3U;
}

/** @brief Scans @p rootDir and common build/source subdirectories for runnable ELF binaries and C/C++ `main` sources. */
void discoverWorkspaceTargets(const QString& rootDir, QStringList* outElfs, QStringList* outMainSources) {
    static const QRegularExpression mainRe(QStringLiteral("\\bmain\\s*\\("));
    QList<QPair<QString, int>> queue = {{rootDir, 0}};
    for (const QString& sub : {QStringLiteral("build_meson"), QStringLiteral("build_cmake"), QStringLiteral("builddir"),
                               QStringLiteral("build"), QStringLiteral("bin"), QStringLiteral("src")}) {
        if (const QString cand = QDir(rootDir).filePath(sub); QDir(cand).exists()) queue.append({cand, 1});
    }
    QSet<QString> visitedDirs, seenElfs, seenSrcs;
    while (!queue.isEmpty()) {
        const auto [curPath, depth] = queue.takeFirst();
        const QFileInfo curInfo(curPath);
        const QString canonDir = !curInfo.canonicalFilePath().isEmpty() ? curInfo.canonicalFilePath() : curInfo.absoluteFilePath();
        if (visitedDirs.contains(canonDir)) continue;
        visitedDirs.insert(canonDir);
        const QDir dir(canonDir);
        const bool inBuildSubtree = canonDir != rootDir && QFileInfo(canonDir).fileName().startsWith(QStringLiteral("build"), Qt::CaseInsensitive);
        for (const QFileInfo& entry : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString canonFile = !entry.canonicalFilePath().isEmpty() ? entry.canonicalFilePath() : entry.absoluteFilePath();
            if (outElfs && !seenElfs.contains(canonFile) && isRunnableElfExecutable(canonFile)) {
                seenElfs.insert(canonFile);
                outElfs->append(canonFile);
            } else if (outMainSources && !inBuildSubtree && !seenSrcs.contains(canonFile)) {
                const QString ext = entry.suffix().toLower();
                if ((ext == QStringLiteral("cpp") || ext == QStringLiteral("cc") || ext == QStringLiteral("cxx") || ext == QStringLiteral("c")) && entry.size() <= 524288) {
                    QFile sf(canonFile);
                    if (sf.open(QIODevice::ReadOnly | QIODevice::Text) && QString::fromUtf8(sf.read(65536)).contains(mainRe)) {
                        seenSrcs.insert(canonFile);
                        outMainSources->append(canonFile);
                    }
                }
            }
        }
        if (depth >= 4) continue;
        for (const QFileInfo& subDir : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString dn = subDir.fileName();
            if (dn.startsWith(QLatin1Char('.')) || dn == QStringLiteral("CMakeFiles") || dn == QStringLiteral("meson-private") ||
                dn == QStringLiteral("Testing") || dn.endsWith(QStringLiteral(".p")) || dn.endsWith(QStringLiteral(".dir"))) continue;
            queue.append({subDir.absoluteFilePath(), depth + 1});
        }
    }
}

/** @brief Returns the resolved source file path for the currently active target. */
QString MainWindow::activeTargetSourcePath() const {
    if (!m_currentFile.trimmed().isEmpty()) return resolveWorkspacePath(m_currentFile);
    if (m_targetSelector) {
        const QString selData = m_targetSelector->currentData().toString();
        if (!selData.isEmpty() && selData != QStringLiteral("__open_binary__")) {
            const QString resolvedSel = resolveWorkspacePath(selData);
            if (!isRunnableElfExecutable(resolvedSel)) return resolvedSel;
        }
    }
    if (!m_activeWorkspaceDir.isEmpty()) return m_activeWorkspaceDir;
    return resolveWorkspacePath(QStringLiteral("tutorial/inspection_demo.cpp"));
}

/** @brief Returns the resolved output executable binary path for the currently active target. */
QString MainWindow::activeTargetBinaryPath() const {
    if (m_targetSelector) {
        const QString selData = m_targetSelector->currentData().toString();
        if (!selData.isEmpty() && selData != QStringLiteral("__open_binary__")) {
            const QString resolvedSel = resolveWorkspacePath(selData);
            if (isRunnableElfExecutable(resolvedSel)) return resolvedSel;
        }
    }
    const QString srcPath = activeTargetSourcePath();
    if (srcPath.isEmpty() || QFileInfo(srcPath).isDir()) return m_loadedBinaryPath;
    const QFileInfo srcInfo(srcPath);
    return srcInfo.dir().filePath(srcInfo.completeBaseName());
}

/** @brief Compiles the active C/C++ MPI target with `-g -O0` on-demand and loads its ELF64 + DWARF info. */
bool MainWindow::ensureTargetBinaryCompiled() {
    m_previewFixtureOnly = false; m_useRealBinaryTarget = true;
    const QString srcPath = activeTargetSourcePath(), binPath = activeTargetBinaryPath();
    if (binPath.isEmpty()) return false;

    const QFileInfo srcInfo(srcPath), binInfo(binPath);
    const bool directElfSelected = m_targetSelector &&
                                   resolveWorkspacePath(m_targetSelector->currentData().toString()) == binInfo.absoluteFilePath() &&
                                   isRunnableElfExecutable(binPath);
    const bool srcExists = !directElfSelected && !srcPath.isEmpty() && srcInfo.exists() && srcInfo.isFile();
    const bool needCompile = srcExists && (!binInfo.exists() || binInfo.lastModified() < srcInfo.lastModified());

    if (needCompile) {
        const bool isCFile = srcInfo.suffix().compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0;
        const QStringList searchDirs = {QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"),
                                        QStringLiteral("/usr/lib64/mpich/bin"), QStringLiteral("/usr/lib/mpich/bin"),
                                        QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")};
        const QString mpiName = isCFile ? QStringLiteral("mpicc") : QStringLiteral("mpicxx");
        const QString gnuName = isCFile ? QStringLiteral("gcc") : QStringLiteral("g++");
        QString compiler = QStandardPaths::findExecutable(mpiName);
        if (compiler.isEmpty()) compiler = QStandardPaths::findExecutable(mpiName, searchDirs);
        if (compiler.isEmpty()) compiler = QStandardPaths::findExecutable(gnuName);
        auto failCompile = [&](const QString& msg) {
            appendProgramOutput(-1, msg, true); flushProgramOutputBatch(); selectBottomToolGroup(QStringLiteral("Debug"));
            if (auto* po = findChild<QWidget*>(QStringLiteral("ProgramOutputDock")); po && m_bottomTabs) m_bottomTabs->setCurrentWidget(po);
            updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("BUILD FAILED")); updateDebugToolbarState();
            return false;
        };
        if (compiler.isEmpty()) return failCompile(QStringLiteral("[Build Failed] No C/C++ or MPI compiler (%1/%2) found in PATH.").arg(mpiName, gnuName));
        QProcess proc; QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), searchDirs.join(QLatin1Char(':')) + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
        proc.setProcessEnvironment(env); proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(compiler, {QStringLiteral("-g"), QStringLiteral("-O0"), srcPath, QStringLiteral("-o"), binPath});
        const bool finished = proc.waitForFinished(15000);
        const QString diag = QString::fromUtf8(proc.readAll()).trimmed();
        if (!finished || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            return failCompile(QStringLiteral("[Build Failed (exit %1)] %2 -g -O0 \"%3\" -o \"%4\"\n%5")
                                   .arg(finished ? proc.exitCode() : -1)
                                   .arg(compiler, srcPath, binPath, diag.isEmpty() ? proc.errorString() : diag));
        }
    }
    if (!QFileInfo::exists(binPath)) return false;
    const QString absBinPath = QFileInfo(binPath).absoluteFilePath();
    if (needCompile || !m_elfInspector || !m_elfInspector->isValid() || m_loadedBinaryPath != absBinPath || m_dwarfLineToAddress.isEmpty()) {
        if (!loadBinaryTarget(binPath)) return false;
    }
    rebuildDynamicSourceLineMap();
    return true;
}

/** @brief Selects an active target in `#toolbarTargetSelector` by name or path and loads its real ELF + DWARF info. */
bool MainWindow::selectTargetByName(const QString& targetName) {
    m_previewFixtureOnly = false;
    const QString query = targetName.trimmed();
    if (query.isEmpty() || !m_targetSelector) return false;
    int matchedIdx = -1;
    for (int i = 0; i < m_targetSelector->count(); ++i) {
        const QString data = m_targetSelector->itemData(i).toString();
        if (data == QStringLiteral("__open_binary__")) continue;
        if (m_targetSelector->itemText(i).contains(query, Qt::CaseInsensitive) || data.contains(query, Qt::CaseInsensitive)) {
            matchedIdx = i;
            break;
        }
    }
    if (matchedIdx < 0) return false;
    if (m_targetSelector->currentIndex() != matchedIdx) {
        m_targetSelector->blockSignals(true); m_targetSelector->setCurrentIndex(matchedIdx); m_targetSelector->blockSignals(false);
    }
    m_targetSelector->setProperty("gl_confirmed_idx", matchedIdx);
    const QString resolvedPath = resolveWorkspacePath(m_targetSelector->itemData(matchedIdx).toString());
    m_targetSelector->setToolTip(QStringLiteral("Active Target: %1\nPath: %2").arg(m_targetSelector->itemText(matchedIdx), resolvedPath));
    if (QFileInfo(resolvedPath).isDir()) return true;
    if (m_disassemblyView) for (uint64_t oldBp : m_disassemblyView->bookmarkedAddresses()) m_disassemblyView->toggleBookmark(oldBp);
    if (isRunnableElfExecutable(resolvedPath)) { m_useRealBinaryTarget = true; loadBinaryTarget(resolvedPath); }
    else { m_currentFile = resolvedPath; loadSourceFile(resolvedPath); ensureTargetBinaryCompiled(); }
    if (SourceCodeView* sv = sourceCodeView()) {
        for (int bpLine : sv->breakpoints()) {
            if (const uint64_t addr = sourceLineToAddress(bpLine); m_disassemblyView && addr != 0ULL && !m_disassemblyView->isBookmarked(addr)) {
                m_disassemblyView->toggleBookmark(addr);
            }
        }
    }
    if (m_debugSessionActive) stopTargetWorkflow();
    return true;
}

/** @brief Atomically switches the active workspace directory, re-roots `ProjectExplorerWidget`, and discovers targets. */
bool MainWindow::switchWorkspaceDirectory(const QString& dirPath) {
    if (m_switchingWorkspace || dirPath.trimmed().isEmpty()) return false;
    const QFileInfo rawInfo(dirPath.trimmed());
    QDir dir = rawInfo.exists() ? (rawInfo.isDir() ? QDir(rawInfo.absoluteFilePath()) : rawInfo.dir())
                                : (rawInfo.fileName().endsWith(QStringLiteral(".toml"), Qt::CaseInsensitive) ? rawInfo.dir() : QDir(rawInfo.absoluteFilePath()));
    if (dir.dirName() == QStringLiteral(".gridlock")) dir.cdUp();
    if (!dir.exists()) return false;
    const QFileInfo dirInfo(dir.absolutePath());
    const QString resolvedDir = !dirInfo.canonicalFilePath().isEmpty() ? dirInfo.canonicalFilePath() : QDir::cleanPath(dirInfo.absoluteFilePath());

    struct ReentrancyGuard { bool& flag; ~ReentrancyGuard() { flag = false; } } guard{m_switchingWorkspace};
    m_switchingWorkspace = true; m_sessionRestoredEmptyBps = false;
    if (!m_activeWorkspaceDir.isEmpty()) saveSession(false);
    if (m_debugSessionActive) stopTargetWorkflow();
    if (m_editorTabManager) m_editorTabManager->clearInlineVariableValues();
    clearAllInspectionDocks(m_variablesDockWidget, m_registerView, m_callStackDockWidget, m_mpiDiagnosticsWidget, networkLog(), m_memView);
    m_cachedStackFrames.clear(); m_cachedThreads.clear(); m_selectedFrameLevel.clear();
    m_previewFixtureOnly = false; m_useRealBinaryTarget = false; m_elfInspector.reset(); m_loadedBinaryPath.clear();
    if (m_differentialGrid) m_differentialGrid->clearWatches();
    if (m_editorTabManager && (m_currentFile.isEmpty() || !m_currentFile.startsWith(resolvedDir + QLatin1Char('/')))) {
        m_editorTabManager->clearAllTabs(); m_currentFile.clear();
    }
    m_activeWorkspaceDir = resolvedDir;
    m_activeSessionFilePath = QDir(resolvedDir).filePath(QStringLiteral(".gridlock/workspace.toml"));
    gridlock::core::ClangdLspClient::instance().setWorkspaceRoot(resolvedDir);
    if (m_projectExplorerWidget && (m_projectExplorerWidget->rootPath() != resolvedDir || m_projectExplorerWidget->isSymbolsMode() || m_projectExplorerWidget->isStringsMode())) {
        m_projectExplorerWidget->setRootPath(resolvedDir);
    }

    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.setWorkspace(resolvedDir); cfg.setLastOpenDir(resolvedDir); m_persistentBreakpoints = cfg.getBreakpoints();
    const auto loadedPs = cfg.loadProjectSettings();
    for (const auto& w : loadedPs.watchExpressions) if (m_differentialGrid) m_differentialGrid->addVariableColumn(QString::fromStdString(w));
    if (m_terminalDock) {
        const QString psWd = QString::fromStdString(loadedPs.workingDirectory);
        m_terminalDock->setWorkingDirectory((!psWd.isEmpty() && QDir(psWd).exists()) ? psWd : resolvedDir);
    }
    const QString folderName = QFileInfo(resolvedDir).fileName();
    setWindowTitle(QStringLiteral("GridLock - %1").arg(folderName.isEmpty() ? resolvedDir : folderName));

    QStringList elfTargets, mainSources;
    discoverWorkspaceTargets(resolvedDir, &elfTargets, &mainSources);
    if (m_targetSelector) {
        m_targetSelector->blockSignals(true); m_targetSelector->clear();
        for (const QString& elfPath : elfTargets) {
            const QString relDir = QDir(resolvedDir).relativeFilePath(QFileInfo(elfPath).absolutePath());
            m_targetSelector->addItem((relDir.isEmpty() || relDir == QStringLiteral("."))
                                          ? QStringLiteral("%1 (ELF)").arg(QFileInfo(elfPath).fileName())
                                          : QStringLiteral("%1 (%2)").arg(QFileInfo(elfPath).fileName(), relDir), elfPath);
        }
        for (const QString& srcPath : mainSources) {
            m_targetSelector->addItem(elfTargets.isEmpty() ? QFileInfo(srcPath).fileName()
                                                           : QStringLiteral("%1 (Source)").arg(QDir(resolvedDir).relativeFilePath(srcPath)), srcPath);
        }
        if (m_targetSelector->count() == 0) m_targetSelector->addItem(QStringLiteral("%1 (Workspace)").arg(folderName.isEmpty() ? QStringLiteral("Workspace") : folderName), resolvedDir);
        m_targetSelector->addItem(QStringLiteral("Open ELF / Binary..."), QStringLiteral("__open_binary__"));
        m_targetSelector->setCurrentIndex(0); m_targetSelector->setProperty("gl_confirmed_idx", 0); m_targetSelector->blockSignals(false);
    }
    if (!mainSources.isEmpty() && m_currentFile.isEmpty()) loadSourceFile(mainSources.first());
    if (!elfTargets.isEmpty()) {
        m_previewFixtureOnly = false; m_useRealBinaryTarget = true; loadBinaryTarget(elfTargets.first());
        if (!mainSources.isEmpty() && m_elfInspector && m_elfInspector->isValid() && m_dwarfLineToAddress.isEmpty()) {
            m_elfInspector->disassembleSymbol(QStringLiteral("main"), QFileInfo(mainSources.first()).fileName(), &m_dwarfLineToAddress, &m_dwarfAddressToLine);
            rebuildDynamicSourceLineMap();
        }
    } else {
        if (m_projectExplorerWidget) { m_projectExplorerWidget->setSymbols({}); m_projectExplorerWidget->setStrings({}); }
        if (m_disassemblyView) m_disassemblyView->clear();
        if (auto* rf = DynamicAddressRibbonFilter::fromNavBar(m_addressNavBar)) rf->updateFromBinaryAndSession(nullptr, false);
    }
    if (!m_debugSessionActive && m_serverRackView) m_serverRackView->setSessionIdle(m_rankCountSpin ? m_rankCountSpin->value() : 8);
    if (m_projectExplorerWidget) m_projectExplorerWidget->setSymbolsMode(false);
    recordRecentWorkspace(resolvedDir);
    syncDisassemblyDecompilerVisibility(true);
    return true;
}

/** @brief Detects the build system ("Meson", "CMake", "Make", or "SingleFileMPI") for @p targetPath. */
QString MainWindow::detectBuildSystem(const QString& targetPath) const {
    const QString root = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : QDir::currentPath();
    const QString resolved = targetPath.trimmed().isEmpty() ? root : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolved);
    QList<QDir> dirs = {QDir(info.isDir() ? info.absoluteFilePath() : info.absolutePath())};
    if (!m_activeWorkspaceDir.isEmpty() && resolved.startsWith(m_activeWorkspaceDir)) dirs.append(QDir(m_activeWorkspaceDir));
    for (const QDir& d : dirs) {
        if (d.exists(QStringLiteral("meson.build"))) return QStringLiteral("Meson");
        if (d.exists(QStringLiteral("CMakeLists.txt"))) return QStringLiteral("CMake");
        if (d.exists(QStringLiteral("Makefile")) || d.exists(QStringLiteral("GNUmakefile"))) return QStringLiteral("Make");
    }
    return QStringLiteral("SingleFileMPI");
}

/** @brief Constructs the shell build command for a project directory or standalone source file. */
QString MainWindow::buildCommandForPath(const QString& targetPath) const {
    const QString buildSys = detectBuildSystem(targetPath);
    const QString root = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : QDir::currentPath();
    const QString resolved = targetPath.trimmed().isEmpty() ? root : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolved);
    QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!m_activeWorkspaceDir.isEmpty() && resolved.startsWith(m_activeWorkspaceDir) &&
        !QDir(dirPath).exists(QStringLiteral("meson.build")) && !QDir(dirPath).exists(QStringLiteral("CMakeLists.txt")) &&
        !QDir(dirPath).exists(QStringLiteral("Makefile"))) dirPath = m_activeWorkspaceDir;
    const QDir dir(dirPath);
    if (buildSys == QStringLiteral("Meson")) {
        for (const QString& b : {QStringLiteral("build_meson"), QStringLiteral("builddir"), QStringLiteral("build")}) {
            if (dir.exists(b + QStringLiteral("/build.ninja"))) return QStringLiteral("ninja -C \"%1/%2\"").arg(dirPath, b);
        }
        return QStringLiteral("meson setup \"%1/builddir\" \"%1\" && ninja -C \"%1/builddir\"").arg(dirPath);
    }
    if (buildSys == QStringLiteral("CMake")) {
        return QStringLiteral("cmake -S \"%1\" -B \"%1/%2\" -DCMAKE_BUILD_TYPE=Debug && cmake --build \"%1/%2\" -j")
            .arg(dirPath, dir.exists(QStringLiteral("build_cmake")) ? QStringLiteral("build_cmake") : QStringLiteral("build"));
    }
    if (buildSys == QStringLiteral("Make")) return QStringLiteral("make -C \"%1\" -j").arg(dirPath);
    const QFileInfo srcInfo(!info.isDir() ? info.absoluteFilePath() : activeTargetSourcePath());
    const bool isC = srcInfo.suffix().compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0;
    const QString cc = isC ? (!QStandardPaths::findExecutable(QStringLiteral("mpicc")).isEmpty() ? QStringLiteral("mpicc") : QStringLiteral("gcc"))
                           : (!QStandardPaths::findExecutable(QStringLiteral("mpicxx")).isEmpty() ? QStringLiteral("mpicxx") : QStringLiteral("g++"));
    return QStringLiteral("%1 -g -O0 \"%2\" -o \"%3\"").arg(cc, srcInfo.absoluteFilePath(), srcInfo.dir().filePath(srcInfo.completeBaseName()));
}

/** @brief Constructs the shell clean command for a project directory or standalone source file. */
QString MainWindow::cleanCommandForPath(const QString& targetPath) const {
    const QString buildSys = detectBuildSystem(targetPath);
    const QString resolvedPath = targetPath.trimmed().isEmpty() ? QDir::currentPath() : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolvedPath);
    const QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    const QDir dir(dirPath);
    if (buildSys == QStringLiteral("Meson")) {
        const QString buildSubdir = (dir.exists(QStringLiteral("build/build.ninja")) && !dir.exists(QStringLiteral("builddir/build.ninja")))
                                        ? QStringLiteral("build") : QStringLiteral("builddir");
        return QStringLiteral("ninja -C \"%1/%2\" -t clean").arg(dirPath, buildSubdir);
    }
    if (buildSys == QStringLiteral("CMake")) return QStringLiteral("cmake --build \"%1/build\" --target clean").arg(dirPath);
    if (buildSys == QStringLiteral("Make")) return QStringLiteral("make clean -C \"%1\"").arg(dirPath);
    const QFileInfo srcInfo(!info.isDir() ? info.absoluteFilePath() : activeTargetSourcePath());
    return QStringLiteral("rm -f \"%1\"").arg(srcInfo.dir().filePath(srcInfo.completeBaseName()));
}

/** @brief Switches BottomConsoleTabs to Terminal, dispatches the build command, and reloads SingleFileMPI ELF output. */
bool MainWindow::buildTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath();
    selectBottomToolGroup(QStringLiteral("Terminal"));
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    const QString buildSys = detectBuildSystem(targetPath), cmd = buildCommandForPath(targetPath);
    if (cmd.isEmpty()) return false;
    m_terminalDock->sendCommand(cmd);
    if (buildSys == QStringLiteral("SingleFileMPI")) ensureTargetBinaryCompiled();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("BUILDING [%1]").arg(buildSys));
    return true;
}

/** @brief Switches BottomConsoleTabs to Terminal and dispatches the detected clean command. */
bool MainWindow::cleanTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath();
    selectBottomToolGroup(QStringLiteral("Terminal"));
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    const QString buildSys = detectBuildSystem(targetPath), cmd = cleanCommandForPath(targetPath);
    if (cmd.isEmpty()) return false;
    m_terminalDock->sendCommand(cmd);
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("CLEANING [%1]").arg(buildSys));
    return true;
}

/** @brief Switches BottomConsoleTabs to Terminal and dispatches a chained clean && build command. */
bool MainWindow::rebuildTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath();
    selectBottomToolGroup(QStringLiteral("Terminal"));
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    const QString buildSys = detectBuildSystem(targetPath), cleanCmd = cleanCommandForPath(targetPath), buildCmd = buildCommandForPath(targetPath);
    if (cleanCmd.isEmpty() || buildCmd.isEmpty()) return false;
    m_terminalDock->sendCommand(QStringLiteral("%1 && %2").arg(cleanCmd, buildCmd));
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("BUILDING [%1]").arg(buildSys));
    return true;
}

bool MainWindow::attachToProcessWorkflow(qint64 explicitPid) {
    return attachToRunningProcesses(explicitPid > 0 ? QList<qint64>{explicitPid} : QList<qint64>{});
}

bool MainWindow::attachToRunningProcesses(const QList<qint64>& explicitPids) {
    QList<ProcessEntry> targets;
    if (explicitPids.isEmpty()) {
        AttachProcessDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return false;
        if (dlg.isRemoteTargetTabActive()) return attachToRemoteTargets(dlg.selectedRemoteTargets());
        targets = dlg.selectedProcesses();
    } else {
        for (int i = 0; i < explicitPids.size(); ++i) {
            if (explicitPids[i] <= 0) continue;
            ProcessEntry e; e.pid = static_cast<pid_t>(explicitPids[i]); e.mpiRank = i; e.mpiWorldSize = explicitPids.size();
            e.executablePath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(e.pid)).symLinkTarget();
            targets.append(e);
        }
    }
    if (targets.isEmpty()) return false;
    if (m_debugSessionActive) stopTargetWorkflow();
    syncCoordinatorWithConfig();
    if (!m_coordinator) return false;

    bool attached = false;
    if (auto* gdbCoord = qobject_cast<GdbRankCoordinator*>(m_coordinator)) attached = gdbCoord->attachToProcesses(targets);
    else if (auto* dapCoord = qobject_cast<DapCoordinator*>(m_coordinator)) attached = dapCoord->attachToProcesses(targets);
    else attached = m_coordinator->attachToProcess(static_cast<qint64>(targets.first().pid));
    if (!attached) return false;

    const int rankCount = targets.size();
    m_debugSessionActive = true; m_focusedRank = 0; m_latestStates.clear();
    QList<RankState> rankStates;
    if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->beginLiveSession(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r; st.currentState = QStringLiteral("stopped"); m_latestStates[r] = st; rankStates.append(st);
        if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->updateLiveRankRow(r, static_cast<qint64>(targets[r].pid), QStringLiteral("PAUSED"), QStringLiteral("attached"), QStringLiteral("LOCAL PID"), QStringLiteral("0x00000000"), rankCount);
    }
    if (m_serverRackView) m_serverRackView->updateRanks(rankStates);
    if (m_memView) m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK 0 (PID %1)").arg(targets.first().pid));
    if (m_gdbConsoleWidget) m_gdbConsoleWidget->ensureRankCount(rankCount);
    m_coordinator->requestStackAndThreads(0);
    updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("ATTACHED (%1 RANK%2)").arg(rankCount).arg(rankCount > 1 ? QStringLiteral("S") : QString()));
    refreshStatusBarMemoryTelemetry();
    return true;
}

bool MainWindow::attachToRemoteTargets(const QList<RemoteTargetSpec>& explicitTargets) {
    if (explicitTargets.isEmpty()) return false;
    if (m_debugSessionActive) stopTargetWorkflow();
    gridlock::core::ConfigManager::instance().setDebuggerEngine(QStringLiteral("gdb"));
    syncCoordinatorWithConfig();
    auto* gdbCoord = qobject_cast<GdbRankCoordinator*>(m_coordinator);
    if (!gdbCoord || !gdbCoord->attachToRemoteTargets(explicitTargets)) return false;

    const int rankCount = explicitTargets.size();
    m_debugSessionActive = true; m_focusedRank = 0; m_latestStates.clear();
    QList<RankState> rankStates;
    if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->beginLiveSession(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r; st.currentState = QStringLiteral("stopped"); m_latestStates[r] = st; rankStates.append(st);
        if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->updateLiveRankRow(r, 0, QStringLiteral("PAUSED"), QStringLiteral("remote"), explicitTargets[r].endpoint, QStringLiteral("0x00000000"), rankCount);
    }
    if (m_serverRackView) m_serverRackView->updateRanks(rankStates);
    if (m_memView) m_memView->setSourceModeLabel(QStringLiteral("REMOTE RANK 0 (%1)").arg(explicitTargets.first().endpoint));
    if (m_gdbConsoleWidget) m_gdbConsoleWidget->ensureRankCount(rankCount);
    m_coordinator->requestStackAndThreads(0);
    updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("REMOTE (%1)").arg(explicitTargets.first().endpoint));
    refreshStatusBarMemoryTelemetry();
    return true;
}

}  // namespace gridlock::ui
