/**
 * @file MainWindowBuild.cpp
 * @brief Target discovery, build-system pre-flight compilation, workspace switching, and process attachment.
 *
 * Implements tutorial/workspace path resolution, ELF executable scanning, on-demand `-g -O0`
 * compilation, Meson/CMake/Make/SingleFileMPI build/clean/rebuild orchestration, workspace
 * root switching, and local/remote process attachment workflows.
 */

#include "MainWindow.hpp"

#include "../core/hpc/dap/DapCoordinator.hpp"
#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/utils/ClangdLspClient.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/debugger/AttachProcessDialog.hpp"
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

bool validateTargetExecutableForLaunch(const QString& targetPath, bool wasJustCompiled, QString* errorOut);

/** @brief Resolves a matching sibling C/C++ source file (`<target>.cpp`, `.c`, `.cc`, `.cxx`) for an extensionless target path. */
static QString findSiblingSourceFile(const QString& targetPath) {
    const QString trimmed = targetPath.trimmed();
    if (trimmed.isEmpty()) return QString();
    if (const QFileInfo fi(trimmed); (fi.exists() && fi.isDir()) || !fi.suffix().isEmpty()) return QString();
    for (const QString& ext : {QStringLiteral(".cpp"), QStringLiteral(".c"), QStringLiteral(".cc"), QStringLiteral(".cxx")})
        if (const QFileInfo cand(trimmed + ext); cand.exists() && cand.isFile()) return !cand.canonicalFilePath().isEmpty() ? cand.canonicalFilePath() : cand.absoluteFilePath();
    return QString();
}

/** @brief Returns true when @p extLower is a non-code data, config, or documentation extension that must skip compilation. */
static bool isNonCodeDataExtension(const QString& extLower) {
    static const QSet<QString> kDataExts = {QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("json"), QStringLiteral("toml"), QStringLiteral("xml"), QStringLiteral("log")};
    return kDataExts.contains(extLower);
}

/** @brief Returns true when @p extLower is a single-file C/C++ source extension supported by on-demand compilation. */
static bool isCompilableCxxSource(const QString& extLower) {
    static const QSet<QString> kSrcExts = {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("cxx"), QStringLiteral("c++"), QStringLiteral("cp")};
    return kSrcExts.contains(extLower);
}

/** @brief Builds a cache stamp `absPath|mtimeMs|size|curFile` to skip redundant `loadBinaryTarget()` reloads. */
static QString makeBinaryStamp(const QString& binPath, const QString& curFile) {
    const QFileInfo fi(binPath);
    return QStringLiteral("%1|%2|%3|%4").arg(fi.absoluteFilePath()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size()).arg(curFile);
}

/** @brief Resolves and caches the preferred MPI or GNU C/C++ compiler executable path. */
static QString resolveCompilerExecutable(bool isCFile) {
    static QString s_cCompiler, s_cxxCompiler;
    QString& cached = isCFile ? s_cCompiler : s_cxxCompiler;
    if (!cached.isEmpty() && QFileInfo(cached).isExecutable()) return cached;
    const QStringList dirs = {QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"), QStringLiteral("/usr/lib64/mpich/bin"), QStringLiteral("/usr/lib/mpich/bin"), QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")};
    const QString mpi = isCFile ? QStringLiteral("mpicc") : QStringLiteral("mpicxx"), gnu = isCFile ? QStringLiteral("gcc") : QStringLiteral("g++");
    if ((cached = QStandardPaths::findExecutable(mpi)).isEmpty() && (cached = QStandardPaths::findExecutable(mpi, dirs)).isEmpty()) cached = QStandardPaths::findExecutable(gnu);
    return cached;
}

/** @brief Resolves a tutorial target name or relative path across local and AppImage share directories. */
QString resolveTutorialTarget(const QString& target) {
    QString rel = target.trimmed();
    if (rel.isEmpty()) return QString();
    if (!rel.endsWith(QStringLiteral(".cpp")) && !rel.contains(QLatin1Char('/'))) rel = QStringLiteral("tutorial/") + rel + QStringLiteral(".cpp");
    else if (!rel.startsWith(QStringLiteral("tutorial/")) && !rel.startsWith(QLatin1Char('/'))) rel = QStringLiteral("tutorial/") + rel;
    auto canonOrAbs = [](const QString& p) { const QFileInfo fi(p); return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath(); };
    for (const QString& cand : {rel, rel + QStringLiteral(".cpp"), QStringLiteral("../") + rel, QStringLiteral("../") + rel + QStringLiteral(".cpp")})
        if (QFileInfo::exists(cand)) return canonOrAbs(cand);
    const QString envAppDir = qEnvironmentVariable("APPDIR");
    if (!envAppDir.isEmpty()) {
        if (const QString s = envAppDir + QStringLiteral("/usr/share/gridlock/") + rel; QFileInfo::exists(s)) return canonOrAbs(s);
        if (const QString d = envAppDir + QStringLiteral("/") + rel; QFileInfo::exists(d)) return canonOrAbs(d);
    }
    const QString appDirPath = QCoreApplication::applicationDirPath(), shareCand = appDirPath + QStringLiteral("/../share/gridlock/") + rel;
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
        const QString cacheDir = QStringLiteral("/tmp/gridlock_tutorials"), targetBin = cacheDir + QStringLiteral("/") + baseName;
        QDir().mkpath(cacheDir);
        if (QFileInfo::exists(companionBin) && (!QFileInfo::exists(targetBin) || QFileInfo(targetBin).lastModified() < QFileInfo(companionBin).lastModified())) {
            QFile::remove(targetBin); QFile::copy(companionBin, targetBin);
            QFile::setPermissions(targetBin, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
        }
        return targetBin;
    }
    return companionBin;
}

/** @brief Resolves a relative or absolute workspace file path across current and parent directories. */
QString MainWindow::resolveWorkspacePath(const QString& rawPath) {
    for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* mw = qobject_cast<MainWindow*>(w); mw && !mw->property("gl_menus_init").toBool()) {
            if (!mw->findChild<QAction*>(QStringLiteral("actionSignalsAndCatchpoints"))) mw->rebuildDynamicSourceLineMap();
            mw->setProperty("gl_menus_init", true);
        }
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) return QString();
    if (QFileInfo::exists(trimmed)) { const QFileInfo fi(trimmed); return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath(); }
    const QString parentCandidate = QStringLiteral("../") + trimmed;
    if (QFileInfo::exists(parentCandidate)) { const QFileInfo fi(parentCandidate); return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath(); }
    if (QFileInfo(trimmed).suffix().isEmpty() && (!findSiblingSourceFile(trimmed).isEmpty() || !findSiblingSourceFile(parentCandidate).isEmpty()))
        return QFileInfo(!findSiblingSourceFile(trimmed).isEmpty() ? trimmed : parentCandidate).absoluteFilePath();
    return QFileInfo(trimmed).absoluteFilePath();
}

/** @brief Checks whether @p filePath is a runnable 64/32-bit ELF `ET_EXEC`/`ET_DYN` binary. */
bool isRunnableElfExecutable(const QString& filePath) {
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile() || !fi.isExecutable() || fi.size() < 64) return false;
    const QString abs = fi.absoluteFilePath(), name = fi.fileName();
    if (abs.contains(QStringLiteral("/CMakeFiles/")) || abs.contains(QStringLiteral("/meson-private/")) || abs.contains(QStringLiteral(".p/")) || abs.contains(QStringLiteral(".dir/"))) return false;
    if (name.endsWith(QStringLiteral(".so")) || name.contains(QStringLiteral(".so.")) || name.endsWith(QStringLiteral(".o")) || name.endsWith(QStringLiteral(".a")) ||
        name.endsWith(QStringLiteral(".lo")) || name.contains(QStringLiteral("sanitycheck"), Qt::CaseInsensitive) || name.contains(QStringLiteral("CompilerId"), Qt::CaseInsensitive)) return false;
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
    for (const QString& sub : {QStringLiteral("build_meson"), QStringLiteral("build_cmake"), QStringLiteral("builddir"), QStringLiteral("build"), QStringLiteral("bin"), QStringLiteral("src")})
        if (const QString cand = QDir(rootDir).filePath(sub); QDir(cand).exists()) queue.append({cand, 1});
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
                seenElfs.insert(canonFile); outElfs->append(canonFile);
            } else if (outMainSources && !inBuildSubtree && !seenSrcs.contains(canonFile)) {
                const QString ext = entry.suffix().toLower();
                if ((ext == QStringLiteral("cpp") || ext == QStringLiteral("cc") || ext == QStringLiteral("cxx") || ext == QStringLiteral("c")) && entry.size() <= 524288) {
                    QFile sf(canonFile);
                    if (sf.open(QIODevice::ReadOnly | QIODevice::Text) && QString::fromUtf8(sf.read(65536)).contains(mainRe)) {
                        seenSrcs.insert(canonFile); outMainSources->append(canonFile);
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
    if (!m_currentFile.trimmed().isEmpty()) {
        const QString resolvedCur = resolveWorkspacePath(m_currentFile), sib = findSiblingSourceFile(resolvedCur);
        return !sib.isEmpty() ? sib : resolvedCur;
    }
    if (m_targetSelector) {
        if (const QString selData = m_targetSelector->currentData().toString(); !selData.isEmpty() && selData != QStringLiteral("__open_binary__")) {
            const QString resolvedSel = resolveWorkspacePath(selData);
            if (const QString sib = findSiblingSourceFile(resolvedSel); !sib.isEmpty()) return sib;
            if (!isRunnableElfExecutable(resolvedSel)) return resolvedSel;
        }
    }
    return !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : resolveWorkspacePath(QStringLiteral("tutorial/inspection_demo.cpp"));
}

/** @brief Returns the resolved output executable binary path for the currently active target. */
QString MainWindow::activeTargetBinaryPath() const {
    if (m_targetSelector)
        if (const QString selData = m_targetSelector->currentData().toString(); !selData.isEmpty() && selData != QStringLiteral("__open_binary__"))
            if (const QString resolvedSel = resolveWorkspacePath(selData); isRunnableElfExecutable(resolvedSel)) return resolvedSel;
    const QString srcPath = activeTargetSourcePath();
    if (srcPath.isEmpty()) return m_loadedBinaryPath;
    const QFileInfo srcInfo(srcPath);
    if (srcInfo.isDir()) return !m_loadedBinaryPath.isEmpty() ? m_loadedBinaryPath : srcInfo.absoluteFilePath();
    return isCompilableCxxSource(srcInfo.suffix().toLower()) ? ensureWritableTutorialBinaryPath(srcInfo.absoluteFilePath()) : srcInfo.absoluteFilePath();
}

/** @brief Compiles the active C/C++ MPI target with `-g -O0` on-demand, sets `+x`, and validates executability before launch. */
bool MainWindow::ensureTargetBinaryCompiled() {
    m_previewFixtureOnly = false; m_useRealBinaryTarget = true;
    QString srcPath = activeTargetSourcePath(), binPath = activeTargetBinaryPath();
    const QString binSibling = findSiblingSourceFile(binPath);
    if (!binSibling.isEmpty()) srcPath = binSibling;
    auto abortTarget = [&](const QString& msg, const QString& badge) {
        appendProgramOutput(-1, msg, true); flushProgramOutputBatch();
        if (m_gdbConsoleWidget) m_gdbConsoleWidget->appendDebuggerConsoleOutput(-1, msg, true);
        selectBottomToolGroup(QStringLiteral("Debug"));
        if (auto* po = findChild<QWidget*>(QStringLiteral("ProgramOutputDock")); po && m_bottomTabs) m_bottomTabs->setCurrentWidget(po);
        updateStatusTelemetry(m_currentSeekOrPc, badge); updateDebugToolbarState(); return false;
    };
    if (binPath.isEmpty()) return abortTarget(QStringLiteral("[Launch Aborted] No runnable target binary is selected."), QStringLiteral("LAUNCH FAILED"));
    const QFileInfo srcInfo(srcPath), binInfo(binPath);
    const QString srcExt = srcInfo.suffix().toLower();
    const bool directElfSelected = binSibling.isEmpty() && m_targetSelector && resolveWorkspacePath(m_targetSelector->currentData().toString()) == binInfo.absoluteFilePath() && isRunnableElfExecutable(binPath);
    const bool compilableSrc = !directElfSelected && !srcPath.isEmpty() && srcInfo.exists() && srcInfo.isFile() && !isNonCodeDataExtension(srcExt) && isCompilableCxxSource(srcExt);
    const bool needCompile = compilableSrc && (!binInfo.exists() || !binInfo.isFile() || binInfo.lastModified() < srcInfo.lastModified() || !binInfo.isExecutable());
    if (needCompile) {
        if (binInfo.exists() && binInfo.isDir()) return abortTarget(QStringLiteral("[Launch Aborted] Target is a directory: %1").arg(binInfo.absoluteFilePath()), QStringLiteral("LAUNCH FAILED"));
        const bool isCFile = srcExt == QStringLiteral("c"); const QString compiler = resolveCompilerExecutable(isCFile);
        if (compiler.isEmpty()) return abortTarget(QStringLiteral("[Build Failed] No C/C++ or MPI compiler (%1) found in PATH.").arg(isCFile ? QStringLiteral("mpicc/gcc") : QStringLiteral("mpicxx/g++")), QStringLiteral("BUILD FAILED"));
        QProcess proc; QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/lib64/openmpi/bin:/usr/lib/openmpi/bin:/usr/lib64/mpich/bin:/usr/lib/mpich/bin:/usr/bin:/usr/local/bin:") + env.value(QStringLiteral("PATH")));
        proc.setProcessEnvironment(env); proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(compiler, {QStringLiteral("-g"), QStringLiteral("-O0"), srcPath, QStringLiteral("-o"), binPath});
        const bool finished = proc.waitForFinished(15000); const QString diag = QString::fromUtf8(proc.readAll()).trimmed();
        if (!finished || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
            return abortTarget(QStringLiteral("[Build Failed (exit %1)] %2 -g -O0 \"%3\" -o \"%4\"\n%5").arg(finished ? proc.exitCode() : -1).arg(compiler, srcPath, binPath, diag.isEmpty() ? proc.errorString() : diag), QStringLiteral("BUILD FAILED"));
        QFile::setPermissions(binPath, QFile::permissions(binPath) | QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    }
    if (QString execErr; !validateTargetExecutableForLaunch(binPath, needCompile, &execErr)) return abortTarget(QStringLiteral("[Launch Aborted] %1").arg(execErr), QStringLiteral("LAUNCH FAILED"));
    const QString absBinPath = QFileInfo(binPath).absoluteFilePath(), stamp = makeBinaryStamp(binPath, m_currentFile);
    if (needCompile || !m_elfInspector || !m_elfInspector->isValid() || m_loadedBinaryPath != absBinPath || property("gl_bin_stamp").toString() != stamp) {
        if (!loadBinaryTarget(binPath)) return false;
        setProperty("gl_bin_stamp", stamp); rebuildDynamicSourceLineMap();
    } else if (m_dynamicLineToAddress.isEmpty() && !m_currentFile.isEmpty()) rebuildDynamicSourceLineMap();
    return true;
}

/** @brief Selects an active target in `#toolbarTargetSelector` by name or path and loads its real ELF + DWARF info. */
bool MainWindow::selectTargetByName(const QString& targetName) {
    m_previewFixtureOnly = false; const QString query = targetName.trimmed();
    if (query.isEmpty() || !m_targetSelector) return false;
    int matchedIdx = -1;
    for (int i = 0; i < m_targetSelector->count(); ++i)
        if (const QString data = m_targetSelector->itemData(i).toString(); data != QStringLiteral("__open_binary__") && (m_targetSelector->itemText(i).contains(query, Qt::CaseInsensitive) || data.contains(query, Qt::CaseInsensitive))) { matchedIdx = i; break; }
    if (matchedIdx < 0) return false;
    if (m_targetSelector->currentIndex() != matchedIdx) { m_targetSelector->blockSignals(true); m_targetSelector->setCurrentIndex(matchedIdx); m_targetSelector->blockSignals(false); }
    m_targetSelector->setProperty("gl_confirmed_idx", matchedIdx);
    const QString resolvedPath = resolveWorkspacePath(m_targetSelector->itemData(matchedIdx).toString());
    m_targetSelector->setToolTip(QStringLiteral("Active Target: %1\nPath: %2").arg(m_targetSelector->itemText(matchedIdx), resolvedPath));
    if (QFileInfo(resolvedPath).isDir()) { m_currentFile.clear(); m_loadedBinaryPath.clear(); setProperty("gl_bin_stamp", QVariant()); if (m_debugSessionActive) stopTargetWorkflow(); return true; }
    if (m_disassemblyView) for (uint64_t oldBp : m_disassemblyView->bookmarkedAddresses()) m_disassemblyView->toggleBookmark(oldBp);
    if (const QString sib = findSiblingSourceFile(resolvedPath); !sib.isEmpty()) {
        m_currentFile = sib; loadSourceFile(sib); ensureTargetBinaryCompiled();
    } else if (isRunnableElfExecutable(resolvedPath)) {
        m_useRealBinaryTarget = true;
        const QString absResolved = QFileInfo(resolvedPath).absoluteFilePath(), stamp = makeBinaryStamp(resolvedPath, m_currentFile);
        if (!m_elfInspector || !m_elfInspector->isValid() || m_loadedBinaryPath != absResolved || property("gl_bin_stamp").toString() != stamp)
            if (loadBinaryTarget(resolvedPath)) setProperty("gl_bin_stamp", stamp);
    } else {
        m_currentFile = resolvedPath;
        QFile probe(resolvedPath);
        if (!(probe.open(QIODevice::ReadOnly) && probe.read(4) == QByteArrayLiteral("\x7f""ELF")) && QFileInfo(resolvedPath).isFile()) loadSourceFile(resolvedPath);
        static const QStringList kCompilableExts = {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("cxx"), QStringLiteral("cu"), QStringLiteral("f"), QStringLiteral("f90"), QStringLiteral("f95"), QStringLiteral("s")};
        if (kCompilableExts.contains(QFileInfo(resolvedPath).suffix().toLower())) ensureTargetBinaryCompiled();
    }
    if (SourceCodeView* sv = sourceCodeView())
        for (int bpLine : sv->breakpoints())
            if (const uint64_t addr = sourceLineToAddress(bpLine); m_disassemblyView && addr != 0ULL && !m_disassemblyView->isBookmarked(addr)) m_disassemblyView->toggleBookmark(addr);
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
    m_previewFixtureOnly = false; m_useRealBinaryTarget = false; m_elfInspector.reset(); m_loadedBinaryPath.clear(); setProperty("gl_bin_stamp", QVariant());
    if (m_differentialGrid) m_differentialGrid->clearWatches();
    if (m_editorTabManager && (m_currentFile.isEmpty() || !m_currentFile.startsWith(resolvedDir + QLatin1Char('/')))) { m_editorTabManager->clearAllTabs(); m_currentFile.clear(); }
    m_activeWorkspaceDir = resolvedDir; m_activeSessionFilePath = QDir(resolvedDir).filePath(QStringLiteral(".gridlock/workspace.toml"));
    gridlock::core::ClangdLspClient::instance().setWorkspaceRoot(resolvedDir);
    if (m_projectExplorerWidget && (m_projectExplorerWidget->rootPath() != resolvedDir || m_projectExplorerWidget->isSymbolsMode() || m_projectExplorerWidget->isStringsMode()))
        m_projectExplorerWidget->setRootPath(resolvedDir);
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.setWorkspace(resolvedDir); cfg.setLastOpenDir(resolvedDir); m_persistentBreakpoints = cfg.getBreakpoints();
    const auto loadedPs = cfg.loadProjectSettings();
    for (const auto& w : loadedPs.watchExpressions) if (m_differentialGrid) m_differentialGrid->addVariableColumn(QString::fromStdString(w));
    if (m_terminalDock) { const QString psWd = QString::fromStdString(loadedPs.workingDirectory); m_terminalDock->setWorkingDirectory((!psWd.isEmpty() && QDir(psWd).exists()) ? psWd : resolvedDir); }
    const QString folderName = QFileInfo(resolvedDir).fileName();
    setWindowTitle(QStringLiteral("GridLock - %1").arg(folderName.isEmpty() ? resolvedDir : folderName));
    QStringList elfTargets, mainSources; discoverWorkspaceTargets(resolvedDir, &elfTargets, &mainSources);
    if (m_targetSelector) {
        m_targetSelector->blockSignals(true); m_targetSelector->clear();
        for (const QString& elfPath : elfTargets) {
            const QString relDir = QDir(resolvedDir).relativeFilePath(QFileInfo(elfPath).absolutePath());
            m_targetSelector->addItem((relDir.isEmpty() || relDir == QStringLiteral(".")) ? QStringLiteral("%1 (ELF)").arg(QFileInfo(elfPath).fileName()) : QStringLiteral("%1 (%2)").arg(QFileInfo(elfPath).fileName(), relDir), elfPath);
        }
        for (const QString& srcPath : mainSources)
            m_targetSelector->addItem(elfTargets.isEmpty() ? QFileInfo(srcPath).fileName() : QStringLiteral("%1 (Source)").arg(QDir(resolvedDir).relativeFilePath(srcPath)), srcPath);
        if (m_targetSelector->count() == 0) m_targetSelector->addItem(QStringLiteral("%1 (Workspace)").arg(folderName.isEmpty() ? QStringLiteral("Workspace") : folderName), resolvedDir);
        m_targetSelector->addItem(QStringLiteral("Open ELF / Binary..."), QStringLiteral("__open_binary__"));
        m_targetSelector->setCurrentIndex(0); m_targetSelector->setProperty("gl_confirmed_idx", 0); m_targetSelector->blockSignals(false);
    }
    if (!mainSources.isEmpty() && m_currentFile.isEmpty()) loadSourceFile(mainSources.first());
    if (!elfTargets.isEmpty()) {
        m_previewFixtureOnly = false; m_useRealBinaryTarget = true;
        if (loadBinaryTarget(elfTargets.first())) setProperty("gl_bin_stamp", makeBinaryStamp(elfTargets.first(), m_currentFile));
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
    recordRecentWorkspace(resolvedDir); syncDisassemblyDecompilerVisibility(true); return true;
}

/** @brief Detects the build system ("Meson", "CMake", "Make", or "SingleFileMPI") for @p targetPath. */
QString MainWindow::detectBuildSystem(const QString& targetPath) const {
    const QString root = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : QDir::currentPath(), resolved = targetPath.trimmed().isEmpty() ? root : resolveWorkspacePath(targetPath);
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
    const QString buildSys = detectBuildSystem(targetPath), root = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : QDir::currentPath(), resolved = targetPath.trimmed().isEmpty() ? root : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolved);
    QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!m_activeWorkspaceDir.isEmpty() && resolved.startsWith(m_activeWorkspaceDir) && !QDir(dirPath).exists(QStringLiteral("meson.build")) && !QDir(dirPath).exists(QStringLiteral("CMakeLists.txt")) && !QDir(dirPath).exists(QStringLiteral("Makefile"))) dirPath = m_activeWorkspaceDir;
    const QDir dir(dirPath);
    if (buildSys == QStringLiteral("Meson")) {
        for (const QString& b : {QStringLiteral("build_meson"), QStringLiteral("builddir"), QStringLiteral("build")})
            if (dir.exists(b + QStringLiteral("/build.ninja"))) return QStringLiteral("ninja -C \"%1/%2\"").arg(dirPath, b);
        return QStringLiteral("meson setup \"%1/builddir\" \"%1\" && ninja -C \"%1/builddir\"").arg(dirPath);
    }
    if (buildSys == QStringLiteral("CMake")) return QStringLiteral("cmake -S \"%1\" -B \"%1/%2\" -DCMAKE_BUILD_TYPE=Debug && cmake --build \"%1/%2\" -j").arg(dirPath, dir.exists(QStringLiteral("build_cmake")) ? QStringLiteral("build_cmake") : QStringLiteral("build"));
    if (buildSys == QStringLiteral("Make")) return QStringLiteral("make -C \"%1\" -j").arg(dirPath);
    QString singleSrc = !info.isDir() ? info.absoluteFilePath() : activeTargetSourcePath();
    if (const QString sib = findSiblingSourceFile(singleSrc); !sib.isEmpty()) singleSrc = sib;
    const QFileInfo srcInfo(singleSrc); const bool isC = srcInfo.suffix().compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0;
    const QString resolvedCc = resolveCompilerExecutable(isC), cc = resolvedCc.isEmpty() ? (isC ? QStringLiteral("gcc") : QStringLiteral("g++")) : QFileInfo(resolvedCc).fileName();
    return QStringLiteral("%1 -g -O0 \"%2\" -o \"%3\"").arg(cc, srcInfo.absoluteFilePath(), srcInfo.dir().filePath(srcInfo.completeBaseName()));
}

/** @brief Constructs the shell clean command for a project directory or standalone source file. */
QString MainWindow::cleanCommandForPath(const QString& targetPath) const {
    const QString buildSys = detectBuildSystem(targetPath), resolvedPath = targetPath.trimmed().isEmpty() ? QDir::currentPath() : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolvedPath); const QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath(); const QDir dir(dirPath);
    if (buildSys == QStringLiteral("Meson")) return QStringLiteral("ninja -C \"%1/%2\" -t clean").arg(dirPath, (dir.exists(QStringLiteral("build/build.ninja")) && !dir.exists(QStringLiteral("builddir/build.ninja"))) ? QStringLiteral("build") : QStringLiteral("builddir"));
    if (buildSys == QStringLiteral("CMake")) return QStringLiteral("cmake --build \"%1/build\" --target clean").arg(dirPath);
    if (buildSys == QStringLiteral("Make")) return QStringLiteral("make clean -C \"%1\"").arg(dirPath);
    QString singleSrc = !info.isDir() ? info.absoluteFilePath() : activeTargetSourcePath();
    if (const QString sib = findSiblingSourceFile(singleSrc); !sib.isEmpty()) singleSrc = sib;
    const QFileInfo srcInfo(singleSrc); return QStringLiteral("rm -f \"%1\"").arg(srcInfo.dir().filePath(srcInfo.completeBaseName()));
}

/** @brief Switches BottomConsoleTabs to Terminal, dispatches the build command, and reloads SingleFileMPI ELF output. */
bool MainWindow::buildTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath(), buildSys = detectBuildSystem(targetPath), cmd = buildCommandForPath(targetPath);
    if (cmd.isEmpty()) return false;
    selectBottomToolGroup(QStringLiteral("Terminal")); if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    m_terminalDock->sendCommand(cmd); if (buildSys == QStringLiteral("SingleFileMPI")) ensureTargetBinaryCompiled();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("BUILDING [%1]").arg(buildSys)); return true;
}

/** @brief Switches BottomConsoleTabs to Terminal and dispatches the detected clean command. */
bool MainWindow::cleanTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath(), buildSys = detectBuildSystem(targetPath), cmd = cleanCommandForPath(targetPath);
    if (cmd.isEmpty()) return false;
    selectBottomToolGroup(QStringLiteral("Terminal")); if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    m_terminalDock->sendCommand(cmd); updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("CLEANING [%1]").arg(buildSys)); return true;
}

/** @brief Switches BottomConsoleTabs to Terminal and dispatches a chained clean && build command. */
bool MainWindow::rebuildTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath(), buildSys = detectBuildSystem(targetPath), cleanCmd = cleanCommandForPath(targetPath), buildCmd = buildCommandForPath(targetPath);
    if (cleanCmd.isEmpty() || buildCmd.isEmpty()) return false;
    selectBottomToolGroup(QStringLiteral("Terminal")); if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    m_terminalDock->sendCommand(QStringLiteral("%1 && %2").arg(cleanCmd, buildCmd));
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("BUILDING [%1]").arg(buildSys)); return true;
}

bool MainWindow::attachToProcessWorkflow(qint64 explicitPid) { return attachToRunningProcesses(explicitPid > 0 ? QList<qint64>{explicitPid} : QList<qint64>{}); }

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
            ProcessEntry e; e.pid = static_cast<pid_t>(explicitPids[i]); e.mpiRank = i; e.mpiWorldSize = static_cast<int>(explicitPids.size());
            e.executablePath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(e.pid)).symLinkTarget(); targets.append(e);
        }
    }
    if (targets.isEmpty()) return false;
    if (m_debugSessionActive) stopTargetWorkflow();
    syncCoordinatorWithConfig(); if (!m_coordinator) return false;
    const bool attached = qobject_cast<GdbRankCoordinator*>(m_coordinator) ? qobject_cast<GdbRankCoordinator*>(m_coordinator)->attachToProcesses(targets)
                        : (qobject_cast<DapCoordinator*>(m_coordinator) ? qobject_cast<DapCoordinator*>(m_coordinator)->attachToProcesses(targets) : m_coordinator->attachToProcess(static_cast<qint64>(targets.first().pid)));
    if (!attached) return false;
    const int rankCount = targets.size(); m_debugSessionActive = true; m_focusedRank = 0; m_latestStates.clear(); QList<RankState> rankStates;
    if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->beginLiveSession(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r; st.currentState = QStringLiteral("stopped"); m_latestStates[r] = st; rankStates.append(st);
        if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->updateLiveRankRow(r, static_cast<qint64>(targets[r].pid), QStringLiteral("PAUSED"), QStringLiteral("attached"), QStringLiteral("LOCAL PID"), QStringLiteral("0x00000000"), rankCount);
    }
    if (m_serverRackView) m_serverRackView->updateRanks(rankStates);
    if (m_memView) m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK 0 (PID %1)").arg(targets.first().pid));
    if (m_gdbConsoleWidget) m_gdbConsoleWidget->ensureRankCount(rankCount);
    m_coordinator->requestStackAndThreads(0); updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("ATTACHED (%1 RANK%2)").arg(rankCount).arg(rankCount > 1 ? QStringLiteral("S") : QString()));
    refreshStatusBarMemoryTelemetry(); return true;
}

bool MainWindow::attachToRemoteTargets(const QList<RemoteTargetSpec>& explicitTargets) {
    if (explicitTargets.isEmpty()) return false;
    if (m_debugSessionActive) stopTargetWorkflow();
    gridlock::core::ConfigManager::instance().setDebuggerEngine(QStringLiteral("gdb")); syncCoordinatorWithConfig();
    auto* gdbCoord = qobject_cast<GdbRankCoordinator*>(m_coordinator);
    if (!gdbCoord || !gdbCoord->attachToRemoteTargets(explicitTargets)) return false;
    const int rankCount = explicitTargets.size(); m_debugSessionActive = true; m_focusedRank = 0; m_latestStates.clear(); QList<RankState> rankStates;
    if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->beginLiveSession(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r; st.currentState = QStringLiteral("stopped"); m_latestStates[r] = st; rankStates.append(st);
        if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->updateLiveRankRow(r, 0, QStringLiteral("PAUSED"), QStringLiteral("remote"), explicitTargets[r].endpoint, QStringLiteral("0x00000000"), rankCount);
    }
    if (m_serverRackView) m_serverRackView->updateRanks(rankStates);
    if (m_memView) m_memView->setSourceModeLabel(QStringLiteral("REMOTE RANK 0 (%1)").arg(explicitTargets.first().endpoint));
    if (m_gdbConsoleWidget) m_gdbConsoleWidget->ensureRankCount(rankCount);
    m_coordinator->requestStackAndThreads(0); updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("REMOTE (%1)").arg(explicitTargets.first().endpoint));
    refreshStatusBarMemoryTelemetry(); return true;
}

}  // namespace gridlock::ui
