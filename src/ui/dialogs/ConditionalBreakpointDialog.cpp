/**
 * @file ConditionalBreakpointDialog.cpp
 * @brief Implementation of the cursor-anchored breakpoint configuration callout popover.
 *
 * Implements a frameless dark-slate popover card with an antialiased top callout pointer
 * triangle, breakpoint enablement checkbox, multi-rank vs. single-rank suspension scope
 * radio controls, MPI condition presets, and screen-clamped global anchor positioning.
 */

#include "ConditionalBreakpointDialog.hpp"
#include "core/utils/ClangdLspClient.hpp"

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>

namespace gridlock::ui {

namespace {
constexpr int kPopoverWidthPx = 380;
constexpr int kPopoverMinHeightPx = 224;
constexpr int kCalloutCaretHeightPx = 10;
constexpr int kDefaultAnchorCaretOffsetPx = 18;
constexpr int kAnchorVerticalGapPx = 6;
constexpr int kScreenEdgeMarginPx = 8;
}  // namespace

/**
 * @brief Constructs the frameless cursor-anchored breakpoint callout popover.
 * @param currentCondition Initial boolean predicate expression to populate in the condition box.
 * @param parent Optional parent widget.
 */
ConditionalBreakpointDialog::ConditionalBreakpointDialog(const QString& currentCondition,
                                                         QWidget* parent)
    : QDialog(parent) {
    // Configure frameless popup window flags and translucent outer margin for the top callout caret.
    setWindowTitle(QStringLiteral("Line Breakpoint"));
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(kPopoverWidthPx);
    setMinimumHeight(kPopoverMinHeightPx);
    resize(kPopoverWidthPx, 228);

    // Apply compact dark-slate popover stylesheet keeping all interactive controls <= 24px tall.
    setStyleSheet(QStringLiteral(
        "QLabel { color: #d7dae0; background: transparent; font-size: 11px; }"
        "QLabel#breakpointPopoverTitle { color: #e5e9f0; font-size: 12px; font-weight: 700; }"
        "QCheckBox, QRadioButton { color: #d7dae0; background: transparent; font-size: 11px; spacing: 5px; }"
        "QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }"
        "QLineEdit { background: #1e2128; color: #e5e9f0; border: 1px solid #43454a; "
        "border-radius: 4px; padding: 1px 6px; min-height: 22px; max-height: 24px; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QLineEdit:focus { border: 1px solid #528bff; }"
        "QComboBox { background: #1e2128; color: #abb2bf; border: 1px solid #43454a; "
        "border-radius: 4px; padding: 1px 6px; min-height: 22px; max-height: 24px; font-size: 11px; }"
        "QComboBox:hover { border-color: #528bff; color: #ffffff; }"
        "QComboBox QAbstractItemView { background: #1e2128; color: #d7dae0; border: 1px solid #43454a; "
        "selection-background-color: #2c436b; }"
        "QPushButton { background: #35383d; color: #d7dae0; border: 1px solid #4c5057; "
        "border-radius: 4px; padding: 2px 10px; min-height: 20px; max-height: 24px; font-size: 11px; }"
        "QPushButton:hover { background: #3f434a; color: #ffffff; border-color: #528bff; }"
        "QPushButton#removeBreakpointBtn { background: transparent; color: #e06c75; border: 1px solid transparent; "
        "padding: 2px 6px; font-weight: 600; }"
        "QPushButton#removeBreakpointBtn:hover { background: rgba(224, 108, 117, 0.14); border-color: #e06c75; }"
        "QPushButton#doneBreakpointBtn { background: #3574f0; color: #ffffff; border: 1px solid #4a88ff; "
        "font-weight: 600; padding: 2px 14px; }"
        "QPushButton#doneBreakpointBtn:hover { background: #4682fa; }"));

    // Reserve top 10px above the card interior for the upward-pointing callout caret (▲).
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, kCalloutCaretHeightPx + 10, 14, 12);
    rootLayout->setSpacing(7);

    // Header Row: Bold "Line <N> breakpoint" banner with subtle horizontal divider.
    m_titleLabel = new QLabel(QStringLiteral("Line breakpoint"), this);
    m_titleLabel->setObjectName(QStringLiteral("breakpointPopoverTitle"));
    rootLayout->addWidget(m_titleLabel);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: #393b40; border: none;"));
    rootLayout->addWidget(divider);

    // Row 1: Enabled checkbox (checked by default; unchecking disables/removes breakpoint on accept).
    m_enabledCheck = new QCheckBox(QStringLiteral("Enabled"), this);
    m_enabledCheck->setChecked(true);
    rootLayout->addWidget(m_enabledCheck);

    // Row 2: Suspend scope row ("Suspend:" checkbox + "All" / "Rank / Thread" radios + "Make Default").
    auto* suspendRow = new QHBoxLayout();
    suspendRow->setContentsMargins(0, 0, 0, 0);
    suspendRow->setSpacing(8);

    m_suspendCheck = new QCheckBox(QStringLiteral("Suspend:"), this);
    m_suspendCheck->setChecked(true);
    m_suspendAllRadio = new QRadioButton(QStringLiteral("All"), this);
    m_suspendAllRadio->setChecked(true);
    m_suspendRankRadio = new QRadioButton(QStringLiteral("Rank / Thread"), this);

    m_makeDefaultBtn = new QPushButton(QStringLiteral("Make Default"), this);
    m_makeDefaultBtn->setObjectName(QStringLiteral("makeDefaultScopeBtn"));
    m_makeDefaultBtn->setFixedHeight(22);

    suspendRow->addWidget(m_suspendCheck);
    suspendRow->addWidget(m_suspendAllRadio);
    suspendRow->addWidget(m_suspendRankRadio);
    suspendRow->addStretch(1);
    suspendRow->addWidget(m_makeDefaultBtn);
    rootLayout->addLayout(suspendRow);

    // Enable or disable radio buttons dynamically when the Suspend checkbox is toggled.
    connect(m_suspendCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_suspendAllRadio) m_suspendAllRadio->setEnabled(checked);
        if (m_suspendRankRadio) m_suspendRankRadio->setEnabled(checked);
    });
    // Persist user's preferred suspend scope when "Make Default" is clicked.
    connect(m_makeDefaultBtn, &QPushButton::clicked, this, [this]() {
        QSettings settings(QStringLiteral("gridlock"), QStringLiteral("debugger"));
        settings.setValue(QStringLiteral("debugger/default_bp_suspend_scope"), suspendScope());
    });

    // Row 3: Condition caption + monospace predicate input + MPI preset dropdown.
    auto* condLabel = new QLabel(QStringLiteral("Condition:"), this);
    rootLayout->addWidget(condLabel);

    auto* condRow = new QHBoxLayout();
    condRow->setContentsMargins(0, 0, 0, 0);
    condRow->setSpacing(6);

    m_conditionEdit = new QLineEdit(this);
    m_conditionEdit->setObjectName(QStringLiteral("breakpointConditionEdit"));
    m_conditionEdit->setFixedHeight(24);
    m_conditionEdit->setPlaceholderText(QStringLiteral("C/C++ predicate (e.g. rank == 0)"));
    m_conditionEdit->setText(currentCondition);

    // Attach dark-slate LSP member/variable completion popup to m_conditionEdit.
    m_completionModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setWidget(m_conditionEdit);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    if (m_completer->popup()) {
        m_completer->popup()->setStyleSheet(QStringLiteral(
            "QAbstractItemView { background: #1e2128; color: #e5e9f0; border: 1px solid #528bff; "
            "selection-background-color: #2c436b; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"));
    }
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, [this](const QString& chosen) {
                QTimer::singleShot(0, this, [this, chosen]() { applyCompletionItem(chosen); });
            });
    connect(m_conditionEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (!m_completer || !m_completionModel || m_fullSourceText.isEmpty()) return;
        const QStringList candidates = completionsForExpression(text);
        m_completionModel->setStringList(candidates);
        if (!candidates.isEmpty()) {
            m_completer->complete();
        } else if (m_completer->popup()) {
            m_completer->popup()->hide();
        }
    });

    m_presetCombo = new QComboBox(this);
    m_presetCombo->setObjectName(QStringLiteral("breakpointPresetCombo"));
    m_presetCombo->setFixedHeight(24);
    m_presetCombo->setFixedWidth(122);
    m_presetCombo->addItems({
        QStringLiteral("Presets ▾"),
        QStringLiteral("rank == 0"),
        QStringLiteral("rank > 0"),
        QStringLiteral("rank == size - 1"),
        QStringLiteral("i == 0"),
        QStringLiteral("i == 2")
    });

    condRow->addWidget(m_conditionEdit, 1);
    condRow->addWidget(m_presetCombo);
    rootLayout->addLayout(condRow);

    // Populate condition field immediately when an MPI preset item is selected.
    connect(m_presetCombo, &QComboBox::activated, this, [this](int index) {
        if (index > 0 && m_presetCombo) {
            applyQuickPreset(m_presetCombo->itemText(index));
            m_presetCombo->setCurrentIndex(0);
        }
    });
    // Pressing Enter inside the condition text box accepts and closes the popover.
    connect(m_conditionEdit, &QLineEdit::returnPressed, this, &QDialog::accept);

    rootLayout->addStretch(1);

    // Bottom Row: Left "Remove Breakpoint (F9)" link action + Right "Cancel" and primary "Done" buttons.
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 2, 0, 0);
    bottomRow->setSpacing(8);

    m_removeBtn = new QPushButton(QStringLiteral("Remove Breakpoint (F9)"), this);
    m_removeBtn->setObjectName(QStringLiteral("removeBreakpointBtn"));
    m_removeBtn->setFixedHeight(24);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelBtn->setObjectName(QStringLiteral("cancelBreakpointBtn"));
    m_cancelBtn->setFixedHeight(24);

    m_doneBtn = new QPushButton(QStringLiteral("Done"), this);
    m_doneBtn->setObjectName(QStringLiteral("doneBreakpointBtn"));
    m_doneBtn->setFixedHeight(24);
    m_doneBtn->setDefault(true);

    bottomRow->addWidget(m_removeBtn);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_cancelBtn);
    bottomRow->addWidget(m_doneBtn);
    rootLayout->addLayout(bottomRow);

    // Wire Remove Breakpoint button and F9 shortcut to mark breakpoint removed and close.
    auto triggerRemove = [this]() {
        m_removeRequested = true;
        if (m_enabledCheck) {
            m_enabledCheck->setChecked(false);
        }
        accept();
    };
    connect(m_removeBtn, &QPushButton::clicked, this, triggerRemove);
    auto* f9Shortcut = new QShortcut(QKeySequence(Qt::Key_F9), this);
    connect(f9Shortcut, &QShortcut::activated, this, triggerRemove);

    // Wire Cancel to reject cleanly and primary Done button to accept changes and close popover.
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_doneBtn, &QPushButton::clicked, this, &QDialog::accept);
}

/**
 * @brief Updates the header title with the 1-based line number and an elided source preview.
 * @param lineOneBased 1-based source line number.
 * @param sourceSnippet Optional code statement on that line.
 */
void ConditionalBreakpointDialog::setTargetLineContext(int lineOneBased, const QString& sourceSnippet) {
    m_targetLine = lineOneBased;
    if (!m_titleLabel) return;

    QString snippet = sourceSnippet.trimmed();
    if (snippet.length() > 30) {
        snippet = snippet.left(29).trimmed() + QStringLiteral("…");
    }
    const QString summary = snippet.isEmpty()
                                ? QStringLiteral("Line %1 breakpoint").arg(lineOneBased)
                                : QStringLiteral("Line %1 breakpoint — %2").arg(lineOneBased).arg(snippet);
    m_titleLabel->setText(summary);
}

/**
 * @brief Stores full source document context for virtual-buffer `clangd` LSP expression completion.
 * @param filePath Active source file path.
 * @param fullSourceText Full text of the active editor document.
 * @param lineOneBased 1-based breakpoint line number.
 */
void ConditionalBreakpointDialog::setSourceDocumentContext(const QString& filePath,
                                                           const QString& fullSourceText,
                                                           int lineOneBased) {
    m_sourceFilePath = filePath;
    m_fullSourceText = fullSourceText;
    if (lineOneBased > 0) {
        m_targetLine = lineOneBased;
    }
}

/**
 * @brief Queries ClangdLspClient for member/symbol completions matching expr at m_targetLine.
 * @param expr Partial C/C++ condition expression (e.g., "data[i]." or "send_buf.").
 * @return Ordered list of completion candidates.
 */
QStringList ConditionalBreakpointDialog::completionsForExpression(const QString& expr) const {
    if (m_fullSourceText.isEmpty()) return {};
    const int line = std::max(1, m_targetLine);
    return core::ClangdLspClient::instance().completeExpressionAtLine(
        m_sourceFilePath, m_fullSourceText, line, expr);
}

/**
 * @brief Appends or replaces the trailing member/identifier token in m_conditionEdit with completionLabel.
 * @param completionLabel Selected member or variable completion string.
 */
void ConditionalBreakpointDialog::applyCompletionItem(const QString& completionLabel) {
    if (!m_conditionEdit || completionLabel.trimmed().isEmpty()) return;

    const QString current = m_conditionEdit->text();
    const int lastDot = current.lastIndexOf(QLatin1Char('.'));
    const int lastArrow = current.lastIndexOf(QStringLiteral("->"));
    const int lastScope = current.lastIndexOf(QStringLiteral("::"));

    int replaceStart = 0;
    if (lastArrow >= 0 && lastArrow + 2 > replaceStart) {
        replaceStart = lastArrow + 2;
    }
    if (lastScope >= 0 && lastScope + 2 > replaceStart) {
        replaceStart = lastScope + 2;
    }
    if (lastDot >= 0 && lastDot + 1 > replaceStart) {
        replaceStart = lastDot + 1;
    }
    if (replaceStart == 0) {
        // Replace only the trailing identifier token if no '.', '->', or '::' separator is present.
        int idx = current.length();
        while (idx > 0 && (current.at(idx - 1).isLetterOrNumber() || current.at(idx - 1) == QLatin1Char('_'))) {
            --idx;
        }
        replaceStart = idx;
    }

    const QString updated = current.left(replaceStart) + completionLabel.trimmed();
    m_conditionEdit->setText(updated);
    m_conditionEdit->setCursorPosition(updated.length());
    m_conditionEdit->setFocus();
}

/**
 * @brief Returns the active title banner text displayed at the top of the popover.
 * @return Summary banner string.
 */
QString ConditionalBreakpointDialog::targetLineSummaryText() const {
    return m_titleLabel ? m_titleLabel->text() : QString();
}

/**
 * @brief Checks whether the breakpoint remains enabled (not unchecked and not removed).
 * @return True if breakpoint is enabled.
 */
bool ConditionalBreakpointDialog::isBreakpointEnabled() const {
    return !m_removeRequested && m_enabledCheck && m_enabledCheck->isChecked();
}

/**
 * @brief Updates the Enabled checkbox state.
 * @param enabled True to mark enabled; false to uncheck.
 */
void ConditionalBreakpointDialog::setBreakpointEnabled(bool enabled) {
    if (m_enabledCheck) {
        m_enabledCheck->setChecked(enabled);
    }
    if (enabled) {
        m_removeRequested = false;
    }
}

/**
 * @brief Resolves the selected execution suspension scope ("All", "Rank", or "None").
 * @return Scope identifier string.
 */
QString ConditionalBreakpointDialog::suspendScope() const {
    if (m_suspendCheck && !m_suspendCheck->isChecked()) {
        return QStringLiteral("None");
    }
    if (m_suspendRankRadio && m_suspendRankRadio->isChecked()) {
        return QStringLiteral("Rank");
    }
    return QStringLiteral("All");
}

/**
 * @brief Applies a previously stored suspension scope ("All", "Rank", or "None") to the controls.
 * @param scope Scope identifier string.
 */
void ConditionalBreakpointDialog::setSuspendScope(const QString& scope) {
    if (scope.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0) {
        if (m_suspendCheck) m_suspendCheck->setChecked(false);
        return;
    }
    if (m_suspendCheck) m_suspendCheck->setChecked(true);
    if (scope.compare(QStringLiteral("Rank"), Qt::CaseInsensitive) == 0) {
        if (m_suspendRankRadio) m_suspendRankRadio->setChecked(true);
    } else {
        if (m_suspendAllRadio) m_suspendAllRadio->setChecked(true);
    }
}

/**
 * @brief Inserts a preset expression into the condition input field and focuses it for editing.
 * @param expr Boolean predicate expression string.
 */
void ConditionalBreakpointDialog::applyQuickPreset(const QString& expr) {
    if (!m_conditionEdit) return;
    m_conditionEdit->setText(expr.trimmed());
    m_conditionEdit->setFocus();
}

/**
 * @brief Extracts the entered condition with leading/trailing whitespace removed.
 * @return Cleaned predicate expression string.
 */
QString ConditionalBreakpointDialog::condition() const {
    return m_conditionEdit ? m_conditionEdit->text().trimmed() : QString();
}

/**
 * @brief Positions the popover directly beneath globalAnchorPos and aligns the top callout caret.
 * @param globalAnchorPos Global coordinate at the bottom-center of the clicked gutter icon.
 */
void ConditionalBreakpointDialog::positionNearAnchor(const QPoint& globalAnchorPos) {
    const int dlgW = std::max(width(), kPopoverWidthPx);
    const int dlgH = std::max(height(), kPopoverMinHeightPx);

    // Offset left by 18px so the top callout caret (x ≈ 18..24) sits directly beneath globalAnchorPos.
    int targetX = std::max(kScreenEdgeMarginPx, globalAnchorPos.x() - kDefaultAnchorCaretOffsetPx);
    int targetY = globalAnchorPos.y() + kAnchorVerticalGapPx;

    // Clamp popover rectangle to the target monitor's available geometry so it never clips off-screen.
    QScreen* screen = QGuiApplication::screenAt(globalAnchorPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect avail = screen->availableGeometry();
        if (avail.isValid() && avail.width() >= dlgW + 2 * kScreenEdgeMarginPx &&
            avail.height() >= dlgH + 2 * kScreenEdgeMarginPx) {
            targetX = std::clamp(targetX, avail.left() + kScreenEdgeMarginPx,
                                 avail.right() - dlgW - kScreenEdgeMarginPx);
            targetY = std::clamp(targetY, avail.top() + kScreenEdgeMarginPx,
                                 avail.bottom() - dlgH - kScreenEdgeMarginPx);
        }
    }

    // Track horizontal caret center relative to clamped card X so ▲ always points at globalAnchorPos.x().
    m_caretCenterX = std::clamp(globalAnchorPos.x() - targetX, 18, dlgW - 18);
    m_anchoredGlobalPos = QPoint(targetX, targetY);
    m_hasExplicitAnchor = true;
    setAttribute(Qt::WA_Moved, true);
    move(m_anchoredGlobalPos);
    update();
}

/**
 * @brief Anchors the popover beneath globalAnchorPos, shows it, and focuses the condition input.
 * @param globalAnchorPos Global coordinate of the gutter breakpoint marker.
 */
void ConditionalBreakpointDialog::showAtGlobalPos(const QPoint& globalAnchorPos) {
    positionNearAnchor(globalAnchorPos);
    show();
    raise();
    activateWindow();
    if (m_conditionEdit) {
        m_conditionEdit->setFocus();
    }
}

/**
 * @brief Re-enforces cursor-anchored global position on showEvent so parent-centering never overrides it.
 * @param event Qt show event.
 */
void ConditionalBreakpointDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (m_hasExplicitAnchor) {
        move(m_anchoredGlobalPos);
    }
}

/**
 * @brief Consumes context menu events inside the popup so right-clicks never bubble to parent views.
 * @param event Context menu event descriptor.
 */
void ConditionalBreakpointDialog::contextMenuEvent(QContextMenuEvent* event) {
    if (event) {
        event->accept();
    }
}

/**
 * @brief Draws the unified 8px-rounded dark-slate card and top upward callout triangle with antialiasing.
 * @param event Paint event covering the popover geometry.
 */
void ConditionalBreakpointDialog::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Construct rounded card body starting below the 10px top callout pointer strip.
    const qreal topY = static_cast<qreal>(kCalloutCaretHeightPx);
    const QRectF cardRect(0.5, topY + 0.5, width() - 1.0, height() - topY - 1.0);
    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, 8.0, 8.0);

    // Construct upward callout pointer triangle (▲) merging seamlessly into the top card edge.
    const qreal cx = static_cast<qreal>(std::clamp(m_caretCenterX, 18, width() - 18));
    QPainterPath caretPath;
    caretPath.moveTo(cx - 7.5, topY + 1.5);
    caretPath.lineTo(cx, 1.5);
    caretPath.lineTo(cx + 7.5, topY + 1.5);
    caretPath.closeSubpath();

    // Unite card and caret paths so border stroke wraps cleanly around the pointer without a seam.
    const QPainterPath calloutShape = cardPath.united(caretPath);
    painter.setPen(QPen(QColor("#43454a"), 1.2));
    painter.setBrush(QColor("#2b2d30"));
    painter.drawPath(calloutShape);
}

}  // namespace gridlock::ui
