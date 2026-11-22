/**
 * @file ProgrammerCalculatorDialog.cpp
 * @brief Implementation of the floating Programmer's Calculator dialog for GridLock.
 *
 * Implements synchronized Hex/Dec/Oct/Bin base conversion, configurable 8/16/32/64-bit
 * word widths with signed/unsigned two's-complement support, BSWAP endianness toggling,
 * an interactive 64-bit clickable bit grid, IEEE-754 float/double and ASCII readouts,
 * and delegates expression evaluation and bit manipulation to CalculatorEngine.
 */

#include "ProgrammerCalculatorDialog.hpp"
#include "CalculatorEngine.hpp"

#include <functional>

#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

uint64_t ProgrammerCalculatorDialog::widthMask(int widthBits) {
    return calc_engine::widthMask(widthBits);
}

int64_t ProgrammerCalculatorDialog::signExtend(uint64_t val, int widthBits) {
    return calc_engine::signExtend(val, widthBits);
}

QString ProgrammerCalculatorDialog::formatGroupedBinary(uint64_t val, int widthBits) {
    return calc_engine::formatGroupedBinary(val, widthBits);
}

bool ProgrammerCalculatorDialog::evaluateExpression(const QString& expr, uint64_t currentValue,
                                                    int widthBits, bool isSigned,
                                                    uint64_t* outValue, QString* outError) {
    calc_engine::EvalResult res;
    if (!calc_engine::evaluate(expr, currentValue, widthBits, isSigned, 0.0, false, &res, outError)) {
        return false;
    }
    if (outValue) *outValue = res.intValue;
    return true;
}

ProgrammerCalculatorDialog::ProgrammerCalculatorDialog(QWidget* parent)
    : QDialog(parent, Qt::Window) {
    setObjectName(QStringLiteral("ProgrammerCalculatorDialog"));
    setWindowTitle(QStringLiteral("Programmer's Calculator"));
    setModal(false);

    // Load user defaults from QSettings ("tools/calcDefaultWidth" and "tools/calcDefaultSigned").
    QSettings settings(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    const int savedWidth = settings.value(QStringLiteral("tools/calcDefaultWidth"), 64).toInt();
    m_wordWidth = (savedWidth == 8 || savedWidth == 16 || savedWidth == 32 || savedWidth == 64) ? savedWidth : 64;
    m_signedMode = settings.value(QStringLiteral("tools/calcDefaultSigned"), false).toBool();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // 1. Header bar with Word Width selector (64/32/16/8-bit), Signed toggle, BSWAP, and explicit Close button.
    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(6);
    auto makeWidthBtn = [&](const QString& label, const QString& objName, int bits) {
        auto* btn = new QPushButton(label, this);
        btn->setObjectName(objName);
        btn->setCheckable(true);
        btn->setFixedHeight(26);
        connect(btn, &QPushButton::clicked, this, [this, bits]() { setWordWidth(bits); });
        return btn;
    };
    m_width64Btn = makeWidthBtn(QStringLiteral("64-bit (QWORD)"), QStringLiteral("calcWidth64Btn"), 64);
    m_width32Btn = makeWidthBtn(QStringLiteral("32-bit (DWORD)"), QStringLiteral("calcWidth32Btn"), 32);
    m_width16Btn = makeWidthBtn(QStringLiteral("16-bit (WORD)"), QStringLiteral("calcWidth16Btn"), 16);
    m_width8Btn = makeWidthBtn(QStringLiteral("8-bit (BYTE)"), QStringLiteral("calcWidth8Btn"), 8);

    m_signedCheck = new QCheckBox(QStringLiteral("Signed"), this);
    m_signedCheck->setObjectName(QStringLiteral("calcSignedCheck"));
    m_signedCheck->setChecked(m_signedMode);
    connect(m_signedCheck, &QCheckBox::toggled, this, &ProgrammerCalculatorDialog::setSigned);

    m_bswapBtn = new QPushButton(QStringLiteral("BSWAP"), this);
    m_bswapBtn->setObjectName(QStringLiteral("calcBswapBtn"));
    m_bswapBtn->setToolTip(QStringLiteral("Toggle little-endian / big-endian byte order for active width"));
    m_bswapBtn->setFixedHeight(26);
    connect(m_bswapBtn, &QPushButton::clicked, this, &ProgrammerCalculatorDialog::byteSwap);

    m_closeBtn = new QPushButton(QStringLiteral("✕ CLOSE"), this);
    m_closeBtn->setObjectName(QStringLiteral("calcCloseBtn"));
    m_closeBtn->setFixedHeight(26);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);

    topBar->addWidget(m_width64Btn); topBar->addWidget(m_width32Btn);
    topBar->addWidget(m_width16Btn); topBar->addWidget(m_width8Btn);
    topBar->addSpacing(8); topBar->addWidget(m_signedCheck); topBar->addWidget(m_bswapBtn);
    topBar->addStretch(1); topBar->addWidget(m_closeBtn);
    root->addLayout(topBar);

    // 2. Synchronized Base Editors card (HEX, DEC, OCT, BIN + 1-click Copy + IEEE-754/ASCII readouts).
    auto* basesGroup = new QGroupBox(QStringLiteral("Synchronized Base Conversion"), this);
    auto* basesGrid = new QGridLayout(basesGroup);
    basesGrid->setContentsMargins(10, 12, 10, 10);
    basesGrid->setSpacing(6);

    auto addBaseRow = [&](int row, const QString& label, const QString& editObj,
                          const QString& copyObj, int radix, QLineEdit** outEdit) {
        auto* lbl = new QLabel(label, basesGroup); lbl->setFixedWidth(48);
        auto* ed = new QLineEdit(basesGroup); ed->setObjectName(editObj); ed->setFixedHeight(26);
        auto* copyBtn = new QPushButton(QStringLiteral("Copy"), basesGroup);
        copyBtn->setObjectName(copyObj); copyBtn->setFixedSize(56, 26);
        connect(copyBtn, &QPushButton::clicked, this, [ed]() {
            if (QClipboard* cb = QApplication::clipboard()) cb->setText(ed->text());
        });
        connect(ed, &QLineEdit::textChanged, this, [this, ed, radix](const QString& raw) {
            if (m_updatingViews) return;
            uint64_t parsed = 0ULL;
            if (calc_engine::parseBaseInput(raw, radix, m_wordWidth, m_signedMode, &parsed)) {
                m_hasFloatValue = false;
                m_value = parsed;
                syncAllViews(ed);
            }
        });
        basesGrid->addWidget(lbl, row, 0); basesGrid->addWidget(ed, row, 1); basesGrid->addWidget(copyBtn, row, 2);
        *outEdit = ed;
    };
    addBaseRow(0, QStringLiteral("HEX"), QStringLiteral("calcHexEdit"), QStringLiteral("calcHexCopyBtn"), 16, &m_hexEdit);
    addBaseRow(1, QStringLiteral("DEC"), QStringLiteral("calcDecEdit"), QStringLiteral("calcDecCopyBtn"), 10, &m_decEdit);
    addBaseRow(2, QStringLiteral("OCT"), QStringLiteral("calcOctEdit"), QStringLiteral("calcOctCopyBtn"), 8, &m_octEdit);
    addBaseRow(3, QStringLiteral("BIN"), QStringLiteral("calcBinEdit"), QStringLiteral("calcBinCopyBtn"), 2, &m_binEdit);

    m_floatReadoutLabel = new QLabel(basesGroup); m_floatReadoutLabel->setObjectName(QStringLiteral("calcFloatLabel"));
    m_asciiReadoutLabel = new QLabel(basesGroup); m_asciiReadoutLabel->setObjectName(QStringLiteral("calcAsciiLabel"));
    auto* metaRow = new QHBoxLayout();
    metaRow->addWidget(m_floatReadoutLabel, 1); metaRow->addWidget(m_asciiReadoutLabel);
    basesGrid->addLayout(metaRow, 4, 0, 1, 3);
    root->addWidget(basesGroup);

    // 3. Interactive 64-Bit Clickable Bit-Toggle Grid (63..32 on top, 31..0 on bottom, nibble-grouped).
    auto* bitGroup = new QGroupBox(QStringLiteral("Interactive 64-Bit Toggle Grid"), this);
    auto* bitLayout = new QVBoxLayout(bitGroup);
    bitLayout->setContentsMargins(8, 10, 8, 8); bitLayout->setSpacing(6);

    auto addBitRow = [&](int highBit, int lowBit) {
        auto* rowLayout = new QHBoxLayout(); rowLayout->setSpacing(6);
        for (int nibbleHigh = highBit; nibbleHigh >= lowBit; nibbleHigh -= 4) {
            auto* nibbleCol = new QVBoxLayout(); nibbleCol->setSpacing(2);
            auto* idxLbl = new QLabel(QString::number(nibbleHigh), bitGroup);
            idxLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            idxLbl->setObjectName(QStringLiteral("calcBitIndexLabel"));
            nibbleCol->addWidget(idxLbl);

            auto* nibbleBtns = new QHBoxLayout(); nibbleBtns->setSpacing(2);
            for (int b = nibbleHigh; b >= nibbleHigh - 3; --b) {
                auto* btn = new QPushButton(QStringLiteral("0"), bitGroup);
                btn->setObjectName(QStringLiteral("calcBitBtn_%1").arg(b));
                btn->setFixedSize(19, 22);
                btn->setToolTip(QStringLiteral("Bit %1 (0x%2)").arg(b).arg(1ULL << b, 0, 16));
                connect(btn, &QPushButton::clicked, this, [this, b]() { toggleBit(b); });
                m_bitButtons[static_cast<size_t>(b)] = btn;
                nibbleBtns->addWidget(btn);
            }
            nibbleCol->addLayout(nibbleBtns);
            rowLayout->addLayout(nibbleCol);
        }
        bitLayout->addLayout(rowLayout);
    };
    addBitRow(63, 32); addBitRow(31, 0);
    root->addWidget(bitGroup);

    // 4. Bitwise & Arithmetic Expression Evaluator + Full Programmer's Calculator Keypad.
    auto* exprGroup = new QGroupBox(QStringLiteral("Bitwise && Arithmetic Expression Evaluator"), this);
    auto* exprVBox = new QVBoxLayout(exprGroup);
    exprVBox->setContentsMargins(10, 12, 10, 10); exprVBox->setSpacing(8);

    auto* exprRow = new QHBoxLayout(); exprRow->setSpacing(6);
    m_exprEdit = new QLineEdit(exprGroup);
    m_exprEdit->setObjectName(QStringLiteral("calcExprEdit"));
    m_exprEdit->setPlaceholderText(QStringLiteral("e.g., 3.3 * 2, (0x10 << 4) | 0x05, 0b1010 ^ 0xff, $ + 64"));
    m_exprEdit->setFixedHeight(28);
    connect(m_exprEdit, &QLineEdit::returnPressed, this, [this]() { evaluateAndApplyExpression(m_exprEdit->text()); });
    exprRow->addWidget(m_exprEdit, 1);

    auto* radixLbl = new QLabel(QStringLiteral("RADIX:"), exprGroup);
    radixLbl->setObjectName(QStringLiteral("calcBitIndexLabel"));
    exprRow->addWidget(radixLbl);

    auto makeRadixBtn = [&](const QString& label, const QString& objName, int radix) {
        auto* btn = new QPushButton(label, exprGroup);
        btn->setObjectName(objName); btn->setCheckable(true); btn->setFixedSize(44, 28);
        connect(btn, &QPushButton::clicked, this, [this, radix]() { setInputRadix(radix); });
        exprRow->addWidget(btn); return btn;
    };
    m_radixDecBtn = makeRadixBtn(QStringLiteral("DEC"), QStringLiteral("calcRadixDecBtn"), 10);
    m_radixHexBtn = makeRadixBtn(QStringLiteral("HEX"), QStringLiteral("calcRadixHexBtn"), 16);
    m_radixOctBtn = makeRadixBtn(QStringLiteral("OCT"), QStringLiteral("calcRadixOctBtn"), 8);
    m_radixBinBtn = makeRadixBtn(QStringLiteral("BIN"), QStringLiteral("calcRadixBinBtn"), 2);

    auto* ansBtn = new QPushButton(QStringLiteral("ANS"), exprGroup);
    ansBtn->setObjectName(QStringLiteral("calcAnsBtn"));
    ansBtn->setToolTip(QStringLiteral("Insert current register value into expression"));
    ansBtn->setFixedSize(50, 28);
    connect(ansBtn, &QPushButton::clicked, this, &ProgrammerCalculatorDialog::insertCurrentValueToken);
    exprRow->addWidget(ansBtn);
    exprVBox->addLayout(exprRow);

    auto* padGrid = new QGridLayout();
    padGrid->setHorizontalSpacing(6); padGrid->setVerticalSpacing(6);
    for (int c = 0; c < 8; ++c) padGrid->setColumnStretch(c, 1);

    auto makePadBtn = [&](const QString& text, const QString& objName, const QString& role,
                          int r, int c, int cs, std::function<void()> cb) {
        auto* btn = new QPushButton(text, exprGroup);
        btn->setObjectName(objName); btn->setProperty("calcRole", role); btn->setMinimumHeight(32);
        connect(btn, &QPushButton::clicked, this, std::move(cb));
        padGrid->addWidget(btn, r, c, 1, cs); return btn;
    };
    auto makeDigitBtn = [&](int digitVal, const QString& label, int r, int c) {
        const QChar ch = label.at(0);
        m_digitButtons[static_cast<size_t>(digitVal)] = makePadBtn(
            label, QStringLiteral("calcKey_%1").arg(label),
            digitVal >= 10 ? QStringLiteral("hex") : QStringLiteral("digit"),
            r, c, 1, [this, ch]() { pressDigitKey(ch); });
    };

    // Row 0: A, B, 7, 8, 9, AND, OR, AC
    makeDigitBtn(10, QStringLiteral("A"), 0, 0); makeDigitBtn(11, QStringLiteral("B"), 0, 1);
    makeDigitBtn(7, QStringLiteral("7"), 0, 2); makeDigitBtn(8, QStringLiteral("8"), 0, 3); makeDigitBtn(9, QStringLiteral("9"), 0, 4);
    makePadBtn(QStringLiteral("AND"), QStringLiteral("calcOpAndBtn"), QStringLiteral("op"), 0, 5, 1, [this]() { insertOperatorToken(QStringLiteral(" & ")); });
    makePadBtn(QStringLiteral("OR"), QStringLiteral("calcOpOrBtn"), QStringLiteral("op"), 0, 6, 1, [this]() { insertOperatorToken(QStringLiteral(" | ")); });
    makePadBtn(QStringLiteral("AC"), QStringLiteral("calcOpClearBtn"), QStringLiteral("clear"), 0, 7, 1, [this]() { clearAll(); });

    // Row 1: C, D, 4, 5, 6, XOR, NOT, Backspace
    makeDigitBtn(12, QStringLiteral("C"), 1, 0); makeDigitBtn(13, QStringLiteral("D"), 1, 1);
    makeDigitBtn(4, QStringLiteral("4"), 1, 2); makeDigitBtn(5, QStringLiteral("5"), 1, 3); makeDigitBtn(6, QStringLiteral("6"), 1, 4);
    makePadBtn(QStringLiteral("XOR"), QStringLiteral("calcOpXorBtn"), QStringLiteral("op"), 1, 5, 1, [this]() { insertOperatorToken(QStringLiteral(" ^ ")); });
    makePadBtn(QStringLiteral("NOT"), QStringLiteral("calcOpNotBtn"), QStringLiteral("op"), 1, 6, 1, [this]() {
        if (m_exprEdit && m_exprEdit->text().trimmed().isEmpty()) setValue(~m_value);
        else if (m_exprEdit) { m_exprEdit->insert(QStringLiteral("~")); m_exprEdit->setFocus(); }
    });
    makePadBtn(QStringLiteral("⌫"), QStringLiteral("calcKeyBackspace"), QStringLiteral("clear"), 1, 7, 1, [this]() { backspaceExpression(); });

    // Row 2: E, F, 1, 2, 3, <<, >>, (
    makeDigitBtn(14, QStringLiteral("E"), 2, 0); makeDigitBtn(15, QStringLiteral("F"), 2, 1);
    makeDigitBtn(1, QStringLiteral("1"), 2, 2); makeDigitBtn(2, QStringLiteral("2"), 2, 3); makeDigitBtn(3, QStringLiteral("3"), 2, 4);
    makePadBtn(QStringLiteral("<<"), QStringLiteral("calcOpShlBtn"), QStringLiteral("op"), 2, 5, 1, [this]() { insertOperatorToken(QStringLiteral(" << ")); });
    makePadBtn(QStringLiteral(">>"), QStringLiteral("calcOpShrBtn"), QStringLiteral("op"), 2, 6, 1, [this]() { insertOperatorToken(QStringLiteral(" >> ")); });
    makePadBtn(QStringLiteral("("), QStringLiteral("calcKeyLParen"), QStringLiteral("op"), 2, 7, 1, [this]() {
        if (!m_exprEdit) return;
        if (m_justEvaluated) { m_exprEdit->clear(); m_justEvaluated = false; }
        m_exprEdit->insert(QStringLiteral("(")); m_exprEdit->setFocus();
    });

    // Row 3: ROL, ROR, ±, 0, ., ÷, ×, )
    makePadBtn(QStringLiteral("ROL"), QStringLiteral("calcOpRolBtn"), QStringLiteral("op"), 3, 0, 1, [this]() { rotateLeft(1); });
    makePadBtn(QStringLiteral("ROR"), QStringLiteral("calcOpRorBtn"), QStringLiteral("op"), 3, 1, 1, [this]() { rotateRight(1); });
    makePadBtn(QStringLiteral("±"), QStringLiteral("calcKeyNegate"), QStringLiteral("op"), 3, 2, 1, [this]() { toggleNegateExpression(); });
    makeDigitBtn(0, QStringLiteral("0"), 3, 3);
    m_dotBtn = makePadBtn(QStringLiteral("."), QStringLiteral("calcKeyDot"), QStringLiteral("digit"), 3, 4, 1, [this]() { pressDotKey(); });
    makePadBtn(QStringLiteral("÷"), QStringLiteral("calcOpDivBtn"), QStringLiteral("op"), 3, 5, 1, [this]() { insertOperatorToken(QStringLiteral(" / ")); });
    makePadBtn(QStringLiteral("×"), QStringLiteral("calcOpMulBtn"), QStringLiteral("op"), 3, 6, 1, [this]() { insertOperatorToken(QStringLiteral(" * ")); });
    makePadBtn(QStringLiteral(")"), QStringLiteral("calcKeyRParen"), QStringLiteral("op"), 3, 7, 1, [this]() {
        if (m_exprEdit) { m_exprEdit->insert(QStringLiteral(")")); m_exprEdit->setFocus(); }
    });

    // Row 4: status label (cols 0..2), %, -, +, and wide highlighted = (cols 6..7)
    m_exprStatusLabel = new QLabel(exprGroup);
    m_exprStatusLabel->setObjectName(QStringLiteral("calcExprStatusLabel"));
    padGrid->addWidget(m_exprStatusLabel, 4, 0, 1, 3);
    makePadBtn(QStringLiteral("%"), QStringLiteral("calcOpModBtn"), QStringLiteral("op"), 4, 3, 1, [this]() { insertOperatorToken(QStringLiteral(" % ")); });
    makePadBtn(QStringLiteral("-"), QStringLiteral("calcOpSubBtn"), QStringLiteral("op"), 4, 4, 1, [this]() { insertOperatorToken(QStringLiteral(" - ")); });
    makePadBtn(QStringLiteral("+"), QStringLiteral("calcOpAddBtn"), QStringLiteral("op"), 4, 5, 1, [this]() { insertOperatorToken(QStringLiteral(" + ")); });
    makePadBtn(QStringLiteral("="), QStringLiteral("calcExprEvalBtn"), QStringLiteral("eval"), 4, 6, 2, [this]() { evaluateAndApplyExpression(m_exprEdit->text()); });

    exprVBox->addLayout(padGrid);
    root->addWidget(exprGroup);

    setMinimumSize(760, 680);
    resize(780, 700);
    syncKeypadRadixState();
    reloadStyle();
    syncAllViews();
}

void ProgrammerCalculatorDialog::setValue(uint64_t val) {
    m_hasFloatValue = false; m_value = val & widthMask(m_wordWidth); syncAllViews();
}

void ProgrammerCalculatorDialog::setWordWidth(int bits) {
    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) return;
    m_wordWidth = bits; m_value &= widthMask(m_wordWidth); syncAllViews();
}

void ProgrammerCalculatorDialog::setSigned(bool enabled) {
    m_signedMode = enabled;
    if (m_signedCheck && m_signedCheck->isChecked() != enabled) {
        m_signedCheck->blockSignals(true); m_signedCheck->setChecked(enabled); m_signedCheck->blockSignals(false);
    }
    syncAllViews();
}

void ProgrammerCalculatorDialog::byteSwap() {
    m_hasFloatValue = false;
    m_value = calc_engine::byteSwap(m_value, m_wordWidth);
    syncAllViews();
}

void ProgrammerCalculatorDialog::rotateLeft(int count) {
    m_hasFloatValue = false;
    m_value = calc_engine::rotateLeft(m_value, m_wordWidth, count);
    syncAllViews();
}

void ProgrammerCalculatorDialog::rotateRight(int count) {
    m_hasFloatValue = false;
    m_value = calc_engine::rotateRight(m_value, m_wordWidth, count);
    syncAllViews();
}

void ProgrammerCalculatorDialog::toggleBit(int bitIndex) {
    if (bitIndex < 0 || bitIndex >= m_wordWidth) return;
    m_hasFloatValue = false;
    m_value = (m_value ^ (1ULL << bitIndex)) & widthMask(m_wordWidth);
    syncAllViews();
}

bool ProgrammerCalculatorDialog::evaluateAndApplyExpression(const QString& expr) {
    calc_engine::EvalResult res;
    QString err;
    if (!calc_engine::evaluate(expr, m_value, m_wordWidth, m_signedMode, m_floatValue, m_hasFloatValue, &res, &err)) {
        if (m_exprStatusLabel) m_exprStatusLabel->setText(QStringLiteral("Error: %1").arg(err));
        return false;
    }
    m_value = res.intValue;
    m_floatValue = res.floatValue;
    m_hasFloatValue = res.isFloat;
    m_justEvaluated = true;
    if (res.isFloat && res.floatValue < 0.0 && !m_signedMode) setSigned(true);
    if (m_exprStatusLabel) {
        const QString decStr = m_signedMode ? QString::number(signExtend(m_value, m_wordWidth)) : QString::number(m_value);
        const QString hexStr = QString::number(m_value, 16).toUpper();
        m_exprStatusLabel->setText(res.isFloat
                                       ? QStringLiteral("Result = %1 (int: %2, 0x%3)").arg(QString::number(res.floatValue, 'g', 12), decStr, hexStr)
                                       : QStringLiteral("Result = 0x%1 (%2)").arg(hexStr, decStr));
    }
    syncAllViews();
    return true;
}

void ProgrammerCalculatorDialog::setInitialTextOrValue(const QString& text) {
    const QString s = calc_engine::normalizeInitialInput(text);
    if (s.isEmpty()) return;
    if (s.startsWith(QLatin1Char('-')) && !s.mid(1).trimmed().contains(QLatin1Char(' '))) setSigned(true);
    calc_engine::EvalResult res;
    QString err;
    if (calc_engine::evaluate(s, m_value, m_wordWidth, m_signedMode, m_floatValue, m_hasFloatValue, &res, &err)) {
        m_value = res.intValue;
        m_floatValue = res.floatValue;
        m_hasFloatValue = res.isFloat;
        m_justEvaluated = true;
        if (res.isFloat && res.floatValue < 0.0 && !m_signedMode) setSigned(true);
        if (m_exprStatusLabel) m_exprStatusLabel->clear();
        if (m_exprEdit) m_exprEdit->setText(calc_engine::formatInitialExpressionText(s, m_value, res.isFloat));
        syncAllViews();
        return;
    }
    if (m_exprEdit) m_exprEdit->setText(s);
    evaluateAndApplyExpression(s);
}

QPushButton* ProgrammerCalculatorDialog::bitButton(int bitIndex) const {
    return (bitIndex >= 0 && bitIndex < 64) ? m_bitButtons[static_cast<size_t>(bitIndex)] : nullptr;
}

QPushButton* ProgrammerCalculatorDialog::keypadDigitButton(int digitVal) const {
    return (digitVal >= 0 && digitVal < 16) ? m_digitButtons[static_cast<size_t>(digitVal)] : nullptr;
}

void ProgrammerCalculatorDialog::setInputRadix(int radix) {
    if (radix != 2 && radix != 8 && radix != 10 && radix != 16) return;
    m_inputRadix = radix; syncKeypadRadixState();
}

void ProgrammerCalculatorDialog::syncKeypadRadixState() {
    if (m_radixDecBtn) m_radixDecBtn->setChecked(m_inputRadix == 10);
    if (m_radixHexBtn) m_radixHexBtn->setChecked(m_inputRadix == 16);
    if (m_radixOctBtn) m_radixOctBtn->setChecked(m_inputRadix == 8);
    if (m_radixBinBtn) m_radixBinBtn->setChecked(m_inputRadix == 2);
    if (m_dotBtn) m_dotBtn->setEnabled(m_inputRadix == 10);
    for (int d = 0; d < 16; ++d) {
        if (QPushButton* btn = m_digitButtons[static_cast<size_t>(d)]) btn->setEnabled(d < m_inputRadix);
    }
}

QString ProgrammerCalculatorDialog::formatValueForRadix(uint64_t val, int radix) const {
    return calc_engine::formatValueForRadix(val, m_wordWidth, radix, m_signedMode, m_floatValue, m_hasFloatValue);
}

void ProgrammerCalculatorDialog::pressDigitKey(QChar digitChar) {
    if (!m_exprEdit) return;
    if (m_justEvaluated) { m_exprEdit->clear(); m_justEvaluated = false; }
    QString next;
    if (!calc_engine::appendDigitToExpression(m_exprEdit->text(), digitChar, m_inputRadix, &next)) return;
    m_exprEdit->setText(next);
    m_exprEdit->setFocus();
}

void ProgrammerCalculatorDialog::pressDotKey() {
    if (!m_exprEdit || m_inputRadix != 10) return;
    if (m_justEvaluated) { m_exprEdit->clear(); m_justEvaluated = false; }
    QString next;
    if (!calc_engine::appendDotToExpression(m_exprEdit->text(), &next)) return;
    m_exprEdit->setText(next);
    m_exprEdit->setFocus();
}

void ProgrammerCalculatorDialog::insertCurrentValueToken() {
    if (!m_exprEdit) return;
    if (m_justEvaluated) { m_exprEdit->clear(); m_justEvaluated = false; }
    m_exprEdit->insert(formatValueForRadix(m_value, m_inputRadix));
    m_exprEdit->setFocus();
}

void ProgrammerCalculatorDialog::backspaceExpression() {
    if (!m_exprEdit) return;
    m_justEvaluated = false;
    if (m_exprEdit->text().isEmpty()) return;
    m_exprEdit->setText(calc_engine::backspaceExpressionText(m_exprEdit->text()));
    m_exprEdit->setFocus();
}

void ProgrammerCalculatorDialog::clearAll() {
    m_justEvaluated = false; m_hasFloatValue = false; m_floatValue = 0.0;
    if (m_exprEdit) m_exprEdit->clear();
    if (m_exprStatusLabel) m_exprStatusLabel->clear();
    setValue(0ULL);
}

void ProgrammerCalculatorDialog::toggleNegateExpression() {
    if (!m_exprEdit) return;
    const QString cur = m_exprEdit->text().trimmed();
    if (cur.isEmpty() || m_justEvaluated) {
        m_justEvaluated = false;
        if (m_hasFloatValue) {
            m_floatValue = -m_floatValue;
            m_value = static_cast<uint64_t>(static_cast<int64_t>(m_floatValue)) & widthMask(m_wordWidth);
            if (m_floatValue < 0.0 && !m_signedMode) setSigned(true); else syncAllViews();
        } else {
            setValue(0ULL - m_value);
        }
        m_exprEdit->setText(formatValueForRadix(m_value, m_inputRadix));
    } else if (cur.startsWith(QStringLiteral("-(")) && cur.endsWith(QLatin1Char(')'))) {
        m_exprEdit->setText(cur.mid(2, cur.size() - 3));
    } else {
        m_exprEdit->setText(QStringLiteral("-(%1)").arg(cur));
    }
    m_exprEdit->setFocus();
}

void ProgrammerCalculatorDialog::insertOperatorToken(const QString& opToken) {
    if (!m_exprEdit) return;
    const QString trimmed = m_exprEdit->text().trimmed();
    if (trimmed.isEmpty() || m_justEvaluated) {
        m_justEvaluated = false;
        m_exprEdit->setText(formatValueForRadix(m_value, m_inputRadix) + opToken);
    } else {
        m_exprEdit->setText(m_exprEdit->text() + opToken);
    }
    m_exprEdit->setFocus();
}

void ProgrammerCalculatorDialog::syncAllViews(QLineEdit* skipEditor) {
    m_updatingViews = true;
    if (m_width64Btn) m_width64Btn->setChecked(m_wordWidth == 64);
    if (m_width32Btn) m_width32Btn->setChecked(m_wordWidth == 32);
    if (m_width16Btn) m_width16Btn->setChecked(m_wordWidth == 16);
    if (m_width8Btn) m_width8Btn->setChecked(m_wordWidth == 8);

    const uint64_t masked = m_value & widthMask(m_wordWidth);
    if (m_hexEdit && skipEditor != m_hexEdit) m_hexEdit->setText(QStringLiteral("0x%1").arg(QString::number(masked, 16).toUpper()));
    if (m_decEdit && skipEditor != m_decEdit) {
        m_decEdit->setText(m_signedMode ? QString::number(signExtend(masked, m_wordWidth)) : QString::number(masked));
    }
    if (m_octEdit && skipEditor != m_octEdit) m_octEdit->setText(QString::number(masked, 8));
    if (m_binEdit && skipEditor != m_binEdit) m_binEdit->setText(formatGroupedBinary(masked, m_wordWidth));

    for (int b = 0; b < 64; ++b) {
        QPushButton* btn = m_bitButtons[static_cast<size_t>(b)];
        if (!btn) continue;
        const bool inRange = (b < m_wordWidth), bitSet = inRange && (((masked >> b) & 1ULL) != 0ULL);
        btn->setEnabled(inRange); btn->setText(bitSet ? QStringLiteral("1") : QStringLiteral("0"));
        btn->setProperty("bitSet", bitSet); btn->style()->unpolish(btn); btn->style()->polish(btn);
    }

    if (m_floatReadoutLabel) m_floatReadoutLabel->setText(calc_engine::formatIeee754Readout(masked, m_floatValue, m_hasFloatValue));
    if (m_asciiReadoutLabel) m_asciiReadoutLabel->setText(calc_engine::formatAsciiReadout(masked, m_wordWidth));
    m_updatingViews = false;
}

void ProgrammerCalculatorDialog::reloadStyle() {
    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
    const QString bg = pal.isDark ? QStringLiteral("#141821") : QStringLiteral("#f8fafc");
    const QString cardBg = pal.isDark ? QStringLiteral("#1b202c") : QStringLiteral("#ffffff");
    const QString inputBg = pal.isDark ? QStringLiteral("#0f131a") : QStringLiteral("#ffffff");
    const QString btnBg = pal.isDark ? QStringLiteral("#252b38") : QStringLiteral("#e2e8f0");
    const QString digitBg = pal.isDark ? QStringLiteral("#2d3548") : QStringLiteral("#cbd5e1");
    const QString muted = pal.isDark ? QStringLiteral("#94a3b8") : QStringLiteral("#475569");
    setStyleSheet(QStringLiteral(
        "QDialog#ProgrammerCalculatorDialog { background: %1; color: %2; }"
        "QGroupBox { background: %3; border: 1px solid %4; border-radius: 5px; margin-top: 8px; padding-top: 8px; font-weight: 600; color: %2; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 6px; color: %5; }"
        "QLineEdit { background: %6; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 6px; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QLineEdit:focus { border: 1px solid %5; }"
        "QPushButton { background: %7; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 6px; font-size: 11px; font-family: 'JetBrains Mono', monospace; }"
        "QPushButton[calcRole] { padding: 4px 8px; margin: 1px; min-height: 22px; }"
        "QPushButton[calcRole=\"digit\"], QPushButton[calcRole=\"hex\"] { background: %9; font-weight: bold; font-size: 12px; }"
        "QPushButton[calcRole=\"eval\"] { background: %5; color: #0b1120; font-weight: bold; font-size: 13px; border: 1px solid %5; }"
        "QPushButton:hover { border: 1px solid %5; }"
        "QPushButton:checked, QPushButton[bitSet=\"true\"] { background: %5; color: #0b1120; font-weight: bold; border: 1px solid %5; }"
        "QPushButton:disabled { opacity: 0.35; color: %8; }"
        "QLabel#calcBitIndexLabel { color: %8; font-size: 9px; font-family: 'JetBrains Mono', monospace; }"
        "QLabel#calcFloatLabel, QLabel#calcAsciiLabel, QLabel#calcExprStatusLabel { color: %8; font-size: 10px; font-family: 'JetBrains Mono', monospace; }")
                      .arg(bg, pal.textHex, cardBg, pal.borderHex, pal.accentHex, inputBg, btnBg, muted, digitBg));
}

}  // namespace gridlock::ui
