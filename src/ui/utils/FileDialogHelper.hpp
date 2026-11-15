#pragma once

/**
 * @file FileDialogHelper.hpp
 * @brief Workspace-scoped file dialog configurator and dynamic ELF section ribbon controller.
 *
 * Provides centralized utilities to:
 * 1. Configure QFileDialog instances so they start in the active workspace root,
 *    prune non-existent ghost bookmarks from sidebarUrls(), and prevent column truncation.
 * 2. Synchronize the top #addressNavBar ELF section ribbon with real parsed ELF64
 *    section headers (.plt, .text, .rodata, .data) and hide the runtime stack pill when idle.
 */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QSet>
#include <QStandardPaths>
#include <QTreeView>
#include <QUrl>
#include <QVariant>
#include <QWidget>

#include "../../core/managers/ThemeManager.hpp"
#include "../../core/utils/ElfBinaryInspector.hpp"

namespace gridlock::ui {

/**
 * @class FileDialogHelper
 * @brief Configures non-native QFileDialogs with workspace-aware defaults, clean bookmarks, and unclipped columns.
 */
class FileDialogHelper {
public:
    /**
     * @brief Normalizes a workspace path (resolving parent directory if given a file or .gridlock path).
     * @param rawPath Candidate workspace directory or file path.
     * @return Canonical or absolute directory path if valid; empty QString otherwise.
     */
    static QString normalizeWorkspaceDir(const QString& rawPath) {
        const QString trimmed = rawPath.trimmed();
        if (trimmed.isEmpty()) return QString();
        QFileInfo info(trimmed);
        if (!info.exists()) return QString();
        QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        QFileInfo dirInfo(dirPath);
        if (dirInfo.fileName() == QStringLiteral(".gridlock")) {
            dirPath = dirInfo.absolutePath();
        }
        const QString canon = QFileInfo(dirPath).canonicalFilePath();
        return !canon.isEmpty() ? canon : QDir::cleanPath(dirPath);
    }

    /**
     * @brief Resolves the initial directory for a file dialog, preferring the active workspace root.
     * @param workspaceDir Active project/workspace root directory.
     * @param requestedDir Optional caller-requested starting directory.
     * @return Existing directory path on disk, falling back to QDir::homePath().
     */
    static QString resolveDefaultDirectory(const QString& workspaceDir,
                                           const QString& requestedDir = QString()) {
        const QString wsDir = normalizeWorkspaceDir(workspaceDir);
        const QString reqDir = normalizeWorkspaceDir(requestedDir);
        const QString appDir = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
        const QString cwdDir = QFileInfo(QDir::currentPath()).canonicalFilePath();

        // Prefer the active workspace directory whenever requestedDir is empty, CWD, or builddir.
        if (!wsDir.isEmpty()) {
            if (reqDir.isEmpty() || reqDir == cwdDir || (!appDir.isEmpty() && reqDir.startsWith(appDir))) {
                return wsDir;
            }
        }
        if (!reqDir.isEmpty()) return reqDir;
        if (!wsDir.isEmpty()) return wsDir;
        return QDir::homePath();
    }

    /**
     * @brief Configures a QFileDialog with active workspace root, pruned sidebar URLs, and unclipped columns.
     * @param dlg Target QFileDialog pointer.
     * @param workspaceDir Active workspace root directory path.
     * @param defaultFileName Optional default file name for Save dialogs.
     */
    static void configureFileDialog(QFileDialog* dlg,
                                    const QString& workspaceDir,
                                    const QString& defaultFileName = QString()) {
        if (!dlg) return;
        // Enable Qt widget layout when dialog is not yet mapped so QTreeView and sidebar are customizable.
        if (!dlg->isVisible()) {
            dlg->setOption(QFileDialog::DontUseNativeDialog, true);
        }

        // 1. Start in active workspace directory (never GridLock's builddir when an external workspace is open).
        const QString currentDir = dlg->directory().absolutePath();
        const QString targetDir = resolveDefaultDirectory(workspaceDir, currentDir);
        if (!targetDir.isEmpty() && QFileInfo::exists(targetDir)) {
            dlg->setDirectory(targetDir);
            if (!defaultFileName.isEmpty()) {
                dlg->selectFile(QDir(targetDir).filePath(defaultFileName));
            }
        }

        // 2. Prune non-existent ghost bookmarks from sidebarUrls() and prepend clean workspace & user folders.
        const QList<QUrl> oldUrls = dlg->sidebarUrls();
        QList<QUrl> cleanUrls;
        QSet<QString> seenCanon;
        auto appendIfExistingDir = [&](const QString& dirCandidate) {
            if (dirCandidate.isEmpty()) return;
            const QFileInfo fi(dirCandidate);
            if (!fi.exists() || !fi.isDir()) return;
            const QString canon = !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
            if (seenCanon.contains(canon)) return;
            seenCanon.insert(canon);
            cleanUrls.append(QUrl::fromLocalFile(canon));
        };

        appendIfExistingDir(targetDir);
        appendIfExistingDir(QDir::homePath());
        appendIfExistingDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        appendIfExistingDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        appendIfExistingDir(QDir::tempPath());

        for (const QUrl& url : oldUrls) {
            if (url.isLocalFile()) {
                appendIfExistingDir(url.toLocalFile());
            }
        }
        dlg->setSidebarUrls(cleanUrls);

        // 3. Enforce generous 920x560 geometry and unclipped QTreeView column resize modes.
        dlg->setMinimumSize(920, 560);
        if (dlg->width() < 920 || dlg->height() < 560) {
            dlg->resize(920, 560);
        }
        for (QTreeView* tv : dlg->findChildren<QTreeView*>()) {
            tv->setMinimumWidth(520);
            tv->setTextElideMode(Qt::ElideNone);
            if (QHeaderView* hdr = tv->header()) {
                hdr->setStretchLastSection(false);
                hdr->setMinimumSectionSize(110);
                if (hdr->count() > 0) {
                    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
                    hdr->resizeSection(0, (std::max)(hdr->sectionSize(0), 340));
                }
                for (int col = 1; col < hdr->count(); ++col) {
                    hdr->setSectionResizeMode(col, QHeaderView::ResizeToContents);
                }
            }
        }
    }

    /**
     * @brief Displays a workspace-scoped, unclipped non-native directory chooser.
     */
    static QString getExistingDirectory(QWidget* parent,
                                        const QString& caption,
                                        const QString& workspaceDir) {
        QFileDialog dlg(parent, caption, resolveDefaultDirectory(workspaceDir));
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        dlg.setOption(QFileDialog::DontResolveSymlinks, true);
        configureFileDialog(&dlg, workspaceDir);
        return (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty())
                   ? dlg.selectedFiles().first()
                   : QString();
    }

    /**
     * @brief Displays a workspace-scoped, unclipped non-native open-file dialog.
     */
    static QString getOpenFileName(QWidget* parent,
                                   const QString& caption,
                                   const QString& workspaceDir,
                                   const QString& filter = QString()) {
        QFileDialog dlg(parent, caption, resolveDefaultDirectory(workspaceDir), filter);
        dlg.setFileMode(QFileDialog::ExistingFile);
        dlg.setAcceptMode(QFileDialog::AcceptOpen);
        configureFileDialog(&dlg, workspaceDir);
        return (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty())
                   ? dlg.selectedFiles().first()
                   : QString();
    }

    /**
     * @brief Displays a workspace-scoped, unclipped non-native save-file dialog.
     */
    static QString getSaveFileName(QWidget* parent,
                                   const QString& caption,
                                   const QString& workspaceDir,
                                   const QString& defaultFileName = QString(),
                                   const QString& filter = QString()) {
        QFileDialog dlg(parent, caption, resolveDefaultDirectory(workspaceDir), filter);
        dlg.setFileMode(QFileDialog::AnyFile);
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        configureFileDialog(&dlg, workspaceDir, defaultFileName);
        return (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty())
                   ? dlg.selectedFiles().first()
                   : QString();
    }
};

/**
 * @class DynamicAddressRibbonFilter
 * @brief Event filter and segment controller binding #addressNavBar to real ELF sections and live stack state.
 */
class DynamicAddressRibbonFilter final : public QObject {
public:
    /**
     * @struct SegEntry
     * @brief Stores section label, virtual address range, layout weight, and theme colors.
     */
    struct SegEntry {
        QString key;
        QString label;
        uint64_t base = 0ULL;
        uint64_t end = 0ULL;
        int weight = 20;
        const char* darkFill = "#1e3038";
        const char* darkFg = "#4ec9b0";
        const char* lightFg = "#1d4ed8";
        bool visible = true;
    };

    /**
     * @brief Constructs and attaches the dynamic section ribbon controller to #addressNavBar.
     * @param navBar Target #addressNavBar widget.
     * @param onSeek Callback invoked when a user clicks a section pill on the ribbon.
     */
    explicit DynamicAddressRibbonFilter(QWidget* navBar,
                                        std::function<void(uint64_t)> onSeek)
        : QObject(navBar), m_navBar(navBar), m_onSeek(std::move(onSeek)) {
        setObjectName(QStringLiteral("dynamicAddressRibbonFilter"));
        // Initialize default segments with stack hidden while no debug session is active.
        m_segments = {
            {QStringLiteral("plt"), QStringLiteral(".plt 0x401000"), 0x00401000ULL, 0x0040111fULL, 14, "#2c2238", "#c586c0", "#b42318", true},
            {QStringLiteral("text"), QStringLiteral(".text 0x401120 (sym.main)"), 0x00401120ULL, 0x00401fffULL, 34, "#1e3038", "#4ec9b0", "#1d4ed8", true},
            {QStringLiteral("rodata"), QStringLiteral(".rodata 0x402000"), 0x00402000ULL, 0x00403fffULL, 18, "#1e2836", "#569cd6", "#0f766e", true},
            {QStringLiteral("data"), QStringLiteral(".data 0x404000"), 0x00404000ULL, 0x00405fffULL, 16, "#2f291e", "#dcdcaa", "#15803d", true},
            {QStringLiteral("stack"), QStringLiteral("stack 0x7fffffffe000"), 0x7fffffffe000ULL, 0x7fffffffffffULL, 18, "#332222", "#ce9178", "#b45309", false},
        };
        if (m_navBar) {
            m_navBar->setProperty("gl_ribbon_filter", true);
            for (const auto& seg : m_segments) {
                auto* lbl = new QLabel(seg.label, m_navBar);
                lbl->setObjectName(QStringLiteral("ribbonSeg_") + seg.key);
                lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                lbl->installEventFilter(this);
                lbl->setVisible(seg.visible);
                m_labels.push_back(lbl);
            }
            m_navBar->installEventFilter(this);
            syncChildWidgets();
        }
    }

    /**
     * @brief Retrieves the installed DynamicAddressRibbonFilter instance attached to @p navBar.
     * @param navBar Target #addressNavBar widget instance.
     * @return Pointer to the installed DynamicAddressRibbonFilter, or nullptr if not present.
     */
    static DynamicAddressRibbonFilter* fromNavBar(QWidget* navBar) {
        return navBar ? static_cast<DynamicAddressRibbonFilter*>(
                            navBar->findChild<QObject*>(QStringLiteral("dynamicAddressRibbonFilter")))
                      : nullptr;
    }

    /**
     * @brief Updates ELF section addresses from ElfBinaryInspector and toggles stack visibility by session state.
     * @param elf Optional loaded ElfBinaryInspector instance.
     * @param sessionActive True when a live or preview debug session is running/paused; false when idle.
     * @param stackRsp Optional live $rsp address for the focused rank.
     */
    void updateFromBinaryAndSession(const gridlock::core::ElfBinaryInspector* elf,
                                    bool sessionActive,
                                    uint64_t stackRsp = 0ULL) {
        if (elf && elf->isValid()) {
            // Map real ELF64 section headers (.plt, .text, .rodata, .data/.bss) into ribbon segments.
            for (const auto& sec : elf->sections()) {
                if (sec.address == 0ULL) continue;
                const uint64_t endAddr = sec.address + (std::max<uint64_t>)(sec.size, 1ULL) - 1ULL;
                const QString hexAddr = QStringLiteral("0x%1").arg(sec.address, 0, 16);
                // Bind .plt procedure linkage table segment base and label.
                if (sec.name == QStringLiteral(".plt")) {
                    m_segments[0].base = sec.address; m_segments[0].end = endAddr;
                    m_segments[0].label = QStringLiteral(".plt ") + hexAddr;
                // Bind .text executable code segment base and label.
                } else if (sec.name == QStringLiteral(".text")) {
                    m_segments[1].base = sec.address; m_segments[1].end = endAddr;
                    m_segments[1].label = QStringLiteral(".text %1 (sym.main)").arg(hexAddr);
                // Bind .rodata read-only constants segment base and label.
                } else if (sec.name == QStringLiteral(".rodata")) {
                    m_segments[2].base = sec.address; m_segments[2].end = endAddr;
                    m_segments[2].label = QStringLiteral(".rodata ") + hexAddr;
                // Bind .data or fallback .bss writable globals segment base and label.
                } else if (sec.name == QStringLiteral(".data") || (sec.name == QStringLiteral(".bss") && m_segments[3].base == 0x00404000ULL)) {
                    m_segments[3].base = sec.address; m_segments[3].end = endAddr;
                    m_segments[3].label = QStringLiteral("%1 %2").arg(sec.name, hexAddr);
                }
            }
        }
        // Hide stack pill when idle; display live $rsp when a debugging session is active.
        m_segments[4].visible = sessionActive;
        if (sessionActive) {
            const uint64_t rspBase = (stackRsp != 0ULL) ? stackRsp : 0x7fffffffe000ULL;
            m_segments[4].base = rspBase;
            m_segments[4].end = rspBase + 0x1fffULL;
            m_segments[4].label = QStringLiteral("stack 0x%1").arg(rspBase, 0, 16);
        }
        syncChildWidgets();
    }

protected:
    /**
     * @brief Renders visible ELF section segments and dispatches segment click-to-seek events.
     * @param watched Watched widget (#addressNavBar or child segment QLabel).
     * @param event Incoming paint, resize, or mouse press event.
     * @return True when custom painting or mouse handling consumed the event.
     */
    bool eventFilter(QObject* watched, QEvent* event) override {
        // Suppress child QLabel native text painting so custom ribbon painter renders crisp single-pass labels.
        if (watched != m_navBar) {
            return (event->type() == QEvent::Paint) ? true : QObject::eventFilter(watched, event);
        }
        // Recompute child QLabel geometries whenever the ribbon bar resizes.
        if (event->type() == QEvent::Resize) {
            syncChildWidgets();
            return false;
        }
        // Map horizontal click position to the clicked visible ELF section and seek to its base address.
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            int totalWeight = 0;
            for (const auto& s : m_segments) if (s.visible) totalWeight += s.weight;
            if (totalWeight <= 0) return true;
            const int w = (std::max)(1, m_navBar->width());
            int x = 0;
            for (size_t i = 0; i < m_segments.size(); ++i) {
                if (!m_segments[i].visible) continue;
                const bool isLast = (i == 4 || (i == 3 && !m_segments[4].visible));
                const int segW = isLast ? (w - x) : (w * m_segments[i].weight) / totalWeight;
                if (me->position().x() >= x && me->position().x() <= x + segW) {
                    m_navBar->setProperty("currentAddress", QVariant::fromValue<qulonglong>(m_segments[i].base));
                    if (m_onSeek) m_onSeek(m_segments[i].base);
                    m_navBar->update();
                    break;
                }
                x += segW;
            }
            return true;
        }
        if (event->type() != QEvent::Paint) return QObject::eventFilter(watched, event);

        // Query active theme palette and current seek address property from #addressNavBar.
        const auto& tm = gridlock::core::ThemeManager::instance();
        const auto pal = tm.paletteForTheme(tm.activeThemeId());
        uint64_t curAddr = m_segments[1].base;
        const QVariant prop = m_navBar->property("currentAddress");
        if (prop.isValid() && prop.toULongLong() != 0ULL) curAddr = prop.toULongLong();

        // Initialize non-antialiased monospace painter for crisp pixel-aligned section blocks.
        QPainter p(m_navBar);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(m_navBar->rect(), QColor(pal.isDark ? pal.canvasHex : pal.headerHex));
        QFont segFont(QStringLiteral("JetBrains Mono"), 8, QFont::DemiBold);
        segFont.setStyleHint(QFont::Monospace);
        p.setFont(segFont);

        int totalWeight = 0;
        for (const auto& s : m_segments) if (s.visible) totalWeight += s.weight;
        if (totalWeight <= 0) return true;

        // Paint each visible ELF section pill and compute the proportional cursor needle offset.
        const int w = m_navBar->width(), h = m_navBar->height();
        int x = 0, activeCursorX = -1;
        for (size_t i = 0; i < m_segments.size(); ++i) {
            const auto& seg = m_segments[i];
            if (!seg.visible) continue;
            const bool isLast = (i == 4 || (i == 3 && !m_segments[4].visible));
            const int segW = isLast ? (w - x) : (w * seg.weight) / totalWeight;
            const bool inSeg = (curAddr >= seg.base && curAddr <= seg.end);
            const QRect segRect(x, 1, (std::max)(1, segW - 1), (std::max)(2, h - 2));
            const QColor fill = pal.isDark ? QColor(QLatin1String(seg.darkFill))
                                           : QColor(inSeg ? pal.canvasHex : pal.headerHex);
            const QColor fg = QColor(QLatin1String(pal.isDark ? seg.darkFg : seg.lightFg));
            p.fillRect(segRect, fill);
            if (!pal.isDark) p.fillRect(QRect(segRect.left(), segRect.bottom() - 1, segRect.width(), 2), fg);
            p.setPen(QColor(pal.borderHex)); p.drawRect(segRect.adjusted(0, 0, -1, -1));
            p.setPen(fg); p.drawText(segRect.adjusted(6, 0, -6, -1), Qt::AlignVCenter | Qt::AlignLeft, seg.label);
            if (inSeg && seg.end > seg.base) {
                const double ratio = std::clamp(static_cast<double>(curAddr - seg.base) /
                                                    static_cast<double>((std::min<uint64_t>)(0x200ULL, seg.end - seg.base)),
                                                0.08, 0.92);
                activeCursorX = x + static_cast<int>(segW * ratio);
            }
            x += segW;
        }
        // Render top and bottom triangular address indicator needles at the active seek coordinate.
        if (activeCursorX < 0) activeCursorX = (w * m_segments[0].weight) / totalWeight + 18;
        const QColor needle = pal.isDark ? QColor(QStringLiteral("#ffcc00")) : QColor(pal.accentHex);
        p.setPen(QPen(needle, 1));
        p.drawLine(activeCursorX, 0, activeCursorX, 4); p.drawLine(activeCursorX, h - 4, activeCursorX, h);
        p.setBrush(needle); p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygon({QPoint(activeCursorX - 4, 0), QPoint(activeCursorX + 4, 0), QPoint(activeCursorX, 4)}));
        p.drawPolygon(QPolygon({QPoint(activeCursorX - 4, h), QPoint(activeCursorX + 4, h), QPoint(activeCursorX, h - 4)}));
        return true;
    }

private:
    /**
     * @brief Synchronizes child QLabel text, visibility, and bounding geometry with active segment states.
     */
    void syncChildWidgets() {
        if (!m_navBar || m_labels.size() != m_segments.size()) return;
        int totalWeight = 0;
        for (const auto& s : m_segments) if (s.visible) totalWeight += s.weight;
        const int w = (std::max)(1, m_navBar->width()), h = (std::max)(1, m_navBar->height());
        int x = 0;
        // Update each child QLabel's text, visibility flag, and proportional horizontal geometry.
        for (size_t i = 0; i < m_segments.size(); ++i) {
            QLabel* lbl = m_labels[i];
            lbl->setText(m_segments[i].label);
            lbl->setVisible(m_segments[i].visible);
            if (m_segments[i].visible && totalWeight > 0) {
                const bool isLast = (i == 4 || (i == 3 && !m_segments[4].visible));
                const int segW = isLast ? (w - x) : (w * m_segments[i].weight) / totalWeight;
                lbl->setGeometry(x, 0, (std::max)(1, segW), h);
                x += segW;
            }
        }
        m_navBar->update();
    }

    QWidget* m_navBar = nullptr;                 ///< Watched #addressNavBar container widget.
    std::vector<SegEntry> m_segments;            ///< Ordered ELF section segment descriptors.
    std::vector<QLabel*> m_labels;               ///< Child QLabel widgets representing each segment.
    std::function<void(uint64_t)> m_onSeek;      ///< Callback invoked on segment click-to-seek.
};

}  // namespace gridlock::ui
