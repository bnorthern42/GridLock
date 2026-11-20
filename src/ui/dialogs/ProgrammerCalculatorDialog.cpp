/**
 * @file ProgrammerCalculatorDialog.cpp
 * @brief Implementation of the floating Programmer's Calculator dialog for GridLock.
 *
 * Implements synchronized Hex/Dec/Oct/Bin base conversion, configurable 8/16/32/64-bit
 * word widths with signed/unsigned two's-complement support, BSWAP endianness toggling,
 * an interactive 64-bit clickable bit grid, IEEE-754 float/double and ASCII readouts,
 * and a recursive-descent C-style integer expression evaluator.
 */

#include "ProgrammerCalculatorDialog.hpp"

#include <cmath>
#include <cstring>

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

namespace {

/**
 * @class IntExprParser
 * @brief Self-contained recursive-descent parser for C-style 64-bit integer expressions.
 *
 * Supports decimal, hexadecimal (0x), binary (0b), octal (0o), '$'/'ans' current value,
 * parentheses, unary (+, -, ~, !), multiplicative (*, /, %), additive (+, -),
 * shift (<<, >>), and bitwise (&, ^, |) operators.
 */
class IntExprParser {
public:
    IntExprParser(const QString& input, uint64_t currentValue, int widthBits, bool isSigned)
        : m_input(input), m_curValue(currentValue), m_width(widthBits), m_signed(isSigned) {}

    bool parse(uint64_t* outValue, QString* outError) {
        skipWs();
        if (m_pos >= m_input.size()) {
            if (outError) *outError = QStringLiteral("Empty expression");
            return false;
        }
        const uint64_t val = parseBitOr();
        skipWs();
        if (!m_error.isEmpty()) {
            if (outError) *outError = m_error;
            return false;
        }
        if (m_pos < m_input.size()) {
            if (outError) *outError = QStringLiteral("Unexpected token at position %1").arg(m_pos + 1);
            return false;
        }
        if (outValue) *outValue = mask(val);
        return true;
    }

private:
    uint64_t mask(uint64_t v) const {
        if (m_width == 8) return v & 0xFFULL;
        if (m_width == 16) return v & 0xFFFFULL;
        if (m_width == 32) return v & 0xFFFFFFFFULL;
        return v;
    }

    int64_t signExtend(uint64_t v) const {
        if (m_width == 8) return static_cast<int64_t>(static_cast<int8_t>(v & 0xFFULL));
        if (m_width == 16) return static_cast<int64_t>(static_cast<int16_t>(v & 0xFFFFULL));
        if (m_width == 32) return static_cast<int64_t>(static_cast<int32_t>(v & 0xFFFFFFFFULL));
        return static_cast<int64_t>(v);
    }

    void skipWs() {
        while (m_pos < m_input.size() && m_input.at(m_pos).isSpace()) ++m_pos;
    }

    bool match(const QString& tok) {
        skipWs();
        if (m_input.mid(m_pos, tok.size()) == tok) {
            m_pos += tok.size();
            return true;
        }
        return false;
    }

    uint64_t parseBitOr() {
        uint64_t lhs = parseBitXor();
        while (m_error.isEmpty() && match(QStringLiteral("|"))) {
            lhs = mask(lhs | parseBitXor());
        }
        return lhs;
    }

    uint64_t parseBitXor() {
        uint64_t lhs = parseBitAnd();
        while (m_error.isEmpty() && match(QStringLiteral("^"))) {
            lhs = mask(lhs ^ parseBitAnd());
        }
        return lhs;
    }

    uint64_t parseBitAnd() {
        uint64_t lhs = parseShift();
        while (m_error.isEmpty() && match(QStringLiteral("&"))) {
            lhs = mask(lhs & parseShift());
        }
        return lhs;
    }

    uint64_t parseShift() {
        uint64_t lhs = parseAddSub();
        while (m_error.isEmpty()) {
            if (match(QStringLiteral("<<"))) {
                const uint64_t sh = parseAddSub();
                lhs = (sh >= static_cast<uint64_t>(m_width)) ? 0ULL : mask(lhs << sh);
            } else if (match(QStringLiteral(">>"))) {
                const uint64_t sh = parseAddSub();
                if (m_signed) {
                    const int64_t s = signExtend(lhs);
                    lhs = (sh >= static_cast<uint64_t>(m_width)) ? (s < 0 ? mask(~0ULL) : 0ULL)
                                                                 : mask(static_cast<uint64_t>(s >> sh));
                } else {
                    lhs = (sh >= static_cast<uint64_t>(m_width)) ? 0ULL : mask(mask(lhs) >> sh);
                }
            } else {
                break;
            }
        }
        return lhs;
    }

    uint64_t parseAddSub() {
        uint64_t lhs = parseMulDiv();
        while (m_error.isEmpty()) {
            if (match(QStringLiteral("+"))) lhs = mask(lhs + parseMulDiv());
            else if (match(QStringLiteral("-"))) lhs = mask(lhs - parseMulDiv());
            else break;
        }
        return lhs;
    }

    uint64_t parseMulDiv() {
        uint64_t lhs = parseUnary();
        while (m_error.isEmpty()) {
            if (match(QStringLiteral("*"))) {
                lhs = mask(lhs * parseUnary());
            } else if (match(QStringLiteral("/"))) {
                const uint64_t rhs = parseUnary();
                if (rhs == 0ULL) { m_error = QStringLiteral("Division by zero"); return 0ULL; }
                if (m_signed) {
                    const int64_t a = signExtend(lhs), b = signExtend(rhs);
                    lhs = (b == -1 && a == INT64_MIN) ? mask(static_cast<uint64_t>(a)) : mask(static_cast<uint64_t>(a / b));
                } else {
                    lhs = mask(mask(lhs) / mask(rhs));
                }
            } else if (match(QStringLiteral("%"))) {
                const uint64_t rhs = parseUnary();
                if (rhs == 0ULL) { m_error = QStringLiteral("Modulo by zero"); return 0ULL; }
                if (m_signed) {
                    const int64_t a = signExtend(lhs), b = signExtend(rhs);
                    lhs = (b == -1 && a == INT64_MIN) ? 0ULL : mask(static_cast<uint64_t>(a % b));
                } else {
                    lhs = mask(mask(lhs) % mask(rhs));
                }
            } else {
                break;
            }
        }
        return lhs;
    }

    uint64_t parseUnary() {
        skipWs();
        if (match(QStringLiteral("+"))) return mask(parseUnary());
        if (match(QStringLiteral("-"))) return mask(0ULL - parseUnary());
        if (match(QStringLiteral("~"))) return mask(~parseUnary());
        if (match(QStringLiteral("!"))) return (mask(parseUnary()) == 0ULL) ? 1ULL : 0ULL;
        return parsePrimary();
    }

    uint64_t parsePrimary() {
        skipWs();
        if (match(QStringLiteral("("))) {
            const uint64_t v = parseBitOr();
            if (!match(QStringLiteral(")"))) m_error = QStringLiteral("Missing closing ')'");
            return mask(v);
        }
        if (match(QStringLiteral("$"))) return mask(m_curValue);
        if (m_input.mid(m_pos, 3).compare(QStringLiteral("ans"), Qt::CaseInsensitive) == 0 &&
            (m_pos + 3 >= m_input.size() || !m_input.at(m_pos + 3).isLetterOrNumber())) {
            m_pos += 3;
            return mask(m_curValue);
        }

        const int start = m_pos;
        while (m_pos < m_input.size() &&
               (m_input.at(m_pos).isLetterOrNumber() || m_input.at(m_pos) == QLatin1Char('_'))) {
            ++m_pos;
        }
        if (m_pos == start) {
            m_error = QStringLiteral("Expected number at position %1").arg(m_pos + 1);
            return 0ULL;
        }

        QString tok = m_input.mid(start, m_pos - start).remove(QLatin1Char('_'));
        int base = 10;
        if (tok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            base = 16;
            tok = tok.mid(2);
        } else if (tok.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) {
            base = 2;
            tok = tok.mid(2);
        } else if (tok.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive)) {
            base = 8;
            tok = tok.mid(2);
        } else if (tok.size() > 1 && tok.startsWith(QLatin1Char('0'))) {
            base = 8;
            tok = tok.mid(1);
        }
        bool ok = false;
        const uint64_t val = tok.toULongLong(&ok, base);
        if (!ok) {
            m_error = QStringLiteral("Invalid integer literal '%1'").arg(tok);
            return 0ULL;
        }
        return mask(val);
    }

    QString m_input;
    int m_pos = 0;
    uint64_t m_curValue = 0ULL;
    int m_width = 64;
    bool m_signed = false;
    QString m_error;
};

}  // namespace

uint64_t ProgrammerCalculatorDialog::widthMask(int widthBits) {
    if (widthBits == 8) return 0xFFULL;
    if (widthBits == 16) return 0xFFFFULL;
    if (widthBits == 32) return 0xFFFFFFFFULL;
    return 0xFFFFFFFFFFFFFFFFULL;
}

int64_t ProgrammerCalculatorDialog::signExtend(uint64_t val, int widthBits) {
    if (widthBits == 8) return static_cast<int64_t>(static_cast<int8_t>(val & 0xFFULL));
    if (widthBits == 16) return static_cast<int64_t>(static_cast<int16_t>(val & 0xFFFFULL));
    if (widthBits == 32) return static_cast<int64_t>(static_cast<int32_t>(val & 0xFFFFFFFFULL));
    return static_cast<int64_t>(val);
}

QString ProgrammerCalculatorDialog::formatGroupedBinary(uint64_t val, int widthBits) {
    QString out;
    out.reserve(widthBits + widthBits / 4);
    for (int bit = widthBits - 1; bit >= 0; --bit) {
        out.append(((val >> bit) & 1ULL) ? QLatin1Char('1') : QLatin1Char('0'));
        if (bit > 0 && (bit % 4) == 0) out.append(QLatin1Char(' '));
    }
    return out;
}

bool ProgrammerCalculatorDialog::evaluateExpression(const QString& expr, uint64_t currentValue,
                                                    int widthBits, bool isSigned,
                                                    uint64_t* outValue, QString* outError) {
    IntExprParser parser(expr, currentValue, widthBits, isSigned);
    return parser.parse(outValue, outError);
}

ProgrammerCalculatorDialog::ProgrammerCalculatorDialog(QWidget* parent)
    : QDialog(parent, Qt::Window) {
    setObjectName(QStringLiteral("ProgrammerCalculatorDialog"));
    setWindowTitle(QStringLiteral("Programmer's Calculator"));
    setModal(false);
    setMinimumSize(720, 490);
    resize(780, 520);

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

    topBar->addWidget(m_width64Btn);
    topBar->addWidget(m_width32Btn);
    topBar->addWidget(m_width16Btn);
    topBar->addWidget(m_width8Btn);
    topBar->addSpacing(8);
    topBar->addWidget(m_signedCheck);
    topBar->addWidget(m_bswapBtn);
    topBar->addStretch(1);
    topBar->addWidget(m_closeBtn);
    root->addLayout(topBar);

    // 2. Synchronized Base Editors card (HEX, DEC, OCT, BIN + 1-click Copy + IEEE-754/ASCII readouts).
    auto* basesGroup = new QGroupBox(QStringLiteral("Synchronized Base Conversion"), this);
    auto* basesGrid = new QGridLayout(basesGroup);
    basesGrid->setContentsMargins(10, 12, 10, 10);
    basesGrid->setSpacing(6);

    auto addBaseRow = [&](int row, const QString& label, const QString& editObj, const QString& copyObj,
                          QLineEdit** outEdit) {
        auto* lbl = new QLabel(label, basesGroup);
        lbl->setFixedWidth(48);
        auto* ed = new QLineEdit(basesGroup);
        ed->setObjectName(editObj);
        ed->setFixedHeight(26);
        auto* copyBtn = new QPushButton(QStringLiteral("Copy"), basesGroup);
        copyBtn->setObjectName(copyObj);
        copyBtn->setFixedSize(56, 26);
        connect(copyBtn, &QPushButton::clicked, this, [ed]() {
            if (QClipboard* cb = QApplication::clipboard()) cb->setText(ed->text());
        });
        basesGrid->addWidget(lbl, row, 0);
        basesGrid->addWidget(ed, row, 1);
        basesGrid->addWidget(copyBtn, row, 2);
        *outEdit = ed;
    };

    addBaseRow(0, QStringLiteral("HEX"), QStringLiteral("calcHexEdit"), QStringLiteral("calcHexCopyBtn"), &m_hexEdit);
    addBaseRow(1, QStringLiteral("DEC"), QStringLiteral("calcDecEdit"), QStringLiteral("calcDecCopyBtn"), &m_decEdit);
    addBaseRow(2, QStringLiteral("OCT"), QStringLiteral("calcOctEdit"), QStringLiteral("calcOctCopyBtn"), &m_octEdit);
    addBaseRow(3, QStringLiteral("BIN"), QStringLiteral("calcBinEdit"), QStringLiteral("calcBinCopyBtn"), &m_binEdit);

    // IEEE-754 Float/Double and ASCII byte interpretation row.
    m_floatReadoutLabel = new QLabel(basesGroup);
    m_floatReadoutLabel->setObjectName(QStringLiteral("calcFloatLabel"));
    m_asciiReadoutLabel = new QLabel(basesGroup);
    m_asciiReadoutLabel->setObjectName(QStringLiteral("calcAsciiLabel"));
    auto* metaRow = new QHBoxLayout();
    metaRow->addWidget(m_floatReadoutLabel, 1);
    metaRow->addWidget(m_asciiReadoutLabel);
    basesGrid->addLayout(metaRow, 4, 0, 1, 3);
    root->addWidget(basesGroup);

    // Connect base editors so editing any base immediately updates all other bases and the bit grid.
    connect(m_hexEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
        if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) s = s.mid(2);
        if (s.isEmpty()) { m_value = 0ULL; syncAllViews(m_hexEdit); return; }
        bool ok = false;
        const uint64_t parsed = s.toULongLong(&ok, 16);
        if (ok) { m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_hexEdit); }
    });
    connect(m_decEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_')).remove(QLatin1Char(','));
        if (s.isEmpty() || s == QStringLiteral("-")) { m_value = 0ULL; syncAllViews(m_decEdit); return; }
        bool ok = false;
        uint64_t parsed = 0ULL;
        if (m_signedMode || s.startsWith(QLatin1Char('-'))) {
            const qint64 sv = s.toLongLong(&ok, 10);
            parsed = static_cast<uint64_t>(sv);
        } else {
            parsed = s.toULongLong(&ok, 10);
        }
        if (ok) { m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_decEdit); }
    });
    connect(m_octEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
        if (s.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive)) s = s.mid(2);
        if (s.isEmpty()) { m_value = 0ULL; syncAllViews(m_octEdit); return; }
        bool ok = false;
        const uint64_t parsed = s.toULongLong(&ok, 8);
        if (ok) { m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_octEdit); }
    });
    connect(m_binEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
        if (s.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) s = s.mid(2);
        if (s.isEmpty()) { m_value = 0ULL; syncAllViews(m_binEdit); return; }
        bool ok = false;
        const uint64_t parsed = s.toULongLong(&ok, 2);
        if (ok) { m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_binEdit); }
    });

    // 3. Interactive 64-Bit Clickable Bit-Toggle Grid (63..32 on top, 31..0 on bottom, nibble-grouped).
    auto* bitGroup = new QGroupBox(QStringLiteral("Interactive 64-Bit Toggle Grid"), this);
    auto* bitLayout = new QVBoxLayout(bitGroup);
    bitLayout->setContentsMargins(8, 10, 8, 8);
    bitLayout->setSpacing(6);

    auto addBitRow = [&](int highBit, int lowBit) {
        auto* rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(6);
        for (int nibbleHigh = highBit; nibbleHigh >= lowBit; nibbleHigh -= 4) {
            auto* nibbleCol = new QVBoxLayout();
            nibbleCol->setSpacing(2);
            auto* idxLbl = new QLabel(QString::number(nibbleHigh), bitGroup);
            idxLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            idxLbl->setObjectName(QStringLiteral("calcBitIndexLabel"));
            nibbleCol->addWidget(idxLbl);

            auto* nibbleBtns = new QHBoxLayout();
            nibbleBtns->setSpacing(2);
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
    addBitRow(63, 32);
    addBitRow(31, 0);
    root->addWidget(bitGroup);

    // 4. Bitwise & Arithmetic Expression Evaluator + Quick Operation Keypad.
    auto* exprGroup = new QGroupBox(QStringLiteral("Bitwise & Arithmetic Expression Evaluator"), this);
    auto* exprVBox = new QVBoxLayout(exprGroup);
    exprVBox->setContentsMargins(10, 12, 10, 8);
    exprVBox->setSpacing(6);

    auto* exprRow = new QHBoxLayout();
    exprRow->setSpacing(6);
    m_exprEdit = new QLineEdit(exprGroup);
    m_exprEdit->setObjectName(QStringLiteral("calcExprEdit"));
    m_exprEdit->setPlaceholderText(QStringLiteral("e.g., (0x10 << 4) | 0x05, 0b1010 ^ 0xff, $ + 64"));
    m_exprEdit->setFixedHeight(26);
    auto* evalBtn = new QPushButton(QStringLiteral("Eval (=)"), exprGroup);
    evalBtn->setObjectName(QStringLiteral("calcExprEvalBtn"));
    evalBtn->setFixedSize(74, 26);
    connect(evalBtn, &QPushButton::clicked, this, [this]() { evaluateAndApplyExpression(m_exprEdit->text()); });
    connect(m_exprEdit, &QLineEdit::returnPressed, this, [this]() { evaluateAndApplyExpression(m_exprEdit->text()); });
    exprRow->addWidget(m_exprEdit, 1);
    exprRow->addWidget(evalBtn);
    exprVBox->addLayout(exprRow);

    auto* keypadRow = new QHBoxLayout();
    keypadRow->setSpacing(4);
    auto addOpBtn = [&](const QString& text, const QString& objName, std::function<void()> cb) {
        auto* btn = new QPushButton(text, exprGroup);
        btn->setObjectName(objName);
        btn->setFixedHeight(24);
        connect(btn, &QPushButton::clicked, this, std::move(cb));
        keypadRow->addWidget(btn);
    };
    addOpBtn(QStringLiteral("AND &"), QStringLiteral("calcOpAndBtn"), [this]() { insertOperatorToken(QStringLiteral(" & ")); });
    addOpBtn(QStringLiteral("OR |"), QStringLiteral("calcOpOrBtn"), [this]() { insertOperatorToken(QStringLiteral(" | ")); });
    addOpBtn(QStringLiteral("XOR ^"), QStringLiteral("calcOpXorBtn"), [this]() { insertOperatorToken(QStringLiteral(" ^ ")); });
    addOpBtn(QStringLiteral("NOT ~"), QStringLiteral("calcOpNotBtn"), [this]() { setValue(~m_value); });
    addOpBtn(QStringLiteral("SHL <<"), QStringLiteral("calcOpShlBtn"), [this]() { setValue(m_value << 1); });
    addOpBtn(QStringLiteral("SHR >>"), QStringLiteral("calcOpShrBtn"), [this]() {
        if (m_signedMode) setValue(static_cast<uint64_t>(signExtend(m_value, m_wordWidth) >> 1));
        else setValue(m_value >> 1);
    });
    addOpBtn(QStringLiteral("+"), QStringLiteral("calcOpAddBtn"), [this]() { insertOperatorToken(QStringLiteral(" + ")); });
    addOpBtn(QStringLiteral("-"), QStringLiteral("calcOpSubBtn"), [this]() { insertOperatorToken(QStringLiteral(" - ")); });
    addOpBtn(QStringLiteral("*"), QStringLiteral("calcOpMulBtn"), [this]() { insertOperatorToken(QStringLiteral(" * ")); });
    addOpBtn(QStringLiteral("/"), QStringLiteral("calcOpDivBtn"), [this]() { insertOperatorToken(QStringLiteral(" / ")); });
    addOpBtn(QStringLiteral("%"), QStringLiteral("calcOpModBtn"), [this]() { insertOperatorToken(QStringLiteral(" % ")); });
    addOpBtn(QStringLiteral("Clear"), QStringLiteral("calcOpClearBtn"), [this]() {
        if (m_exprEdit) m_exprEdit->clear();
        if (m_exprStatusLabel) m_exprStatusLabel->clear();
        setValue(0ULL);
    });
    exprVBox->addLayout(keypadRow);

    m_exprStatusLabel = new QLabel(exprGroup);
    m_exprStatusLabel->setObjectName(QStringLiteral("calcExprStatusLabel"));
    exprVBox->addWidget(m_exprStatusLabel);
    root->addWidget(exprGroup);

    reloadStyle();
    syncAllViews();
}

void ProgrammerCalculatorDialog::setValue(uint64_t val) {
    m_value = val & widthMask(m_wordWidth);
    syncAllViews();
}

void ProgrammerCalculatorDialog::setWordWidth(int bits) {
    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) return;
    m_wordWidth = bits;
    m_value &= widthMask(m_wordWidth);
    syncAllViews();
}

void ProgrammerCalculatorDialog::setSigned(bool enabled) {
    m_signedMode = enabled;
    if (m_signedCheck && m_signedCheck->isChecked() != enabled) {
        m_signedCheck->blockSignals(true);
        m_signedCheck->setChecked(enabled);
        m_signedCheck->blockSignals(false);
    }
    syncAllViews();
}

void ProgrammerCalculatorDialog::byteSwap() {
    if (m_wordWidth == 64) {
        m_value = __builtin_bswap64(m_value);
    } else if (m_wordWidth == 32) {
        m_value = static_cast<uint64_t>(__builtin_bswap32(static_cast<uint32_t>(m_value)));
    } else if (m_wordWidth == 16) {
        m_value = static_cast<uint64_t>(__builtin_bswap16(static_cast<uint16_t>(m_value)));
    }
    m_value &= widthMask(m_wordWidth);
    syncAllViews();
}

void ProgrammerCalculatorDialog::toggleBit(int bitIndex) {
    if (bitIndex < 0 || bitIndex >= m_wordWidth) return;
    m_value = (m_value ^ (1ULL << bitIndex)) & widthMask(m_wordWidth);
    syncAllViews();
}

bool ProgrammerCalculatorDialog::evaluateAndApplyExpression(const QString& expr) {
    uint64_t result = 0ULL;
    QString err;
    if (!evaluateExpression(expr, m_value, m_wordWidth, m_signedMode, &result, &err)) {
        if (m_exprStatusLabel) m_exprStatusLabel->setText(QStringLiteral("Error: %1").arg(err));
        return false;
    }
    m_value = result & widthMask(m_wordWidth);
    if (m_exprStatusLabel) {
        m_exprStatusLabel->setText(
            QStringLiteral("Result = 0x%1").arg(QString::number(m_value, 16).toUpper()));
    }
    syncAllViews();
    return true;
}

QPushButton* ProgrammerCalculatorDialog::bitButton(int bitIndex) const {
    if (bitIndex < 0 || bitIndex >= 64) return nullptr;
    return m_bitButtons[static_cast<size_t>(bitIndex)];
}

void ProgrammerCalculatorDialog::insertOperatorToken(const QString& opToken) {
    if (!m_exprEdit) return;
    if (m_exprEdit->text().trimmed().isEmpty()) {
        m_exprEdit->setText(QStringLiteral("0x%1%2").arg(QString::number(m_value, 16).toUpper(), opToken));
    } else {
        m_exprEdit->insert(opToken);
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
    if (m_hexEdit && skipEditor != m_hexEdit) {
        m_hexEdit->setText(QStringLiteral("0x%1").arg(QString::number(masked, 16).toUpper()));
    }
    if (m_decEdit && skipEditor != m_decEdit) {
        if (m_signedMode) m_decEdit->setText(QString::number(signExtend(masked, m_wordWidth)));
        else m_decEdit->setText(QString::number(masked));
    }
    if (m_octEdit && skipEditor != m_octEdit) {
        m_octEdit->setText(QString::number(masked, 8));
    }
    if (m_binEdit && skipEditor != m_binEdit) {
        m_binEdit->setText(formatGroupedBinary(masked, m_wordWidth));
    }

    // Update interactive 64-bit button states and active width enablement.
    for (int b = 0; b < 64; ++b) {
        QPushButton* btn = m_bitButtons[static_cast<size_t>(b)];
        if (!btn) continue;
        const bool inRange = (b < m_wordWidth);
        const bool bitSet = inRange && (((masked >> b) & 1ULL) != 0ULL);
        btn->setEnabled(inRange);
        btn->setText(bitSet ? QStringLiteral("1") : QStringLiteral("0"));
        btn->setProperty("bitSet", bitSet);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }

    // Update IEEE-754 float (low 32-bit) / double (64-bit) and printable ASCII bytes.
    const uint32_t low32 = static_cast<uint32_t>(masked & 0xFFFFFFFFULL);
    float f32 = 0.0f;
    double f64 = 0.0;
    std::memcpy(&f32, &low32, sizeof(f32));
    std::memcpy(&f64, &masked, sizeof(f64));
    if (m_floatReadoutLabel) {
        m_floatReadoutLabel->setText(
            QStringLiteral("IEEE-754: float = %1  |  double = %2")
                .arg(QString::number(static_cast<double>(f32), 'g', 7),
                     QString::number(f64, 'g', 12)));
    }
    if (m_asciiReadoutLabel) {
        const int byteCount = m_wordWidth / 8;
        QString ascii;
        ascii.reserve(byteCount);
        for (int i = byteCount - 1; i >= 0; --i) {
            const unsigned char ch = static_cast<unsigned char>((masked >> (i * 8)) & 0xFFU);
            ascii.append((ch >= 0x20 && ch <= 0x7E) ? QChar::fromLatin1(static_cast<char>(ch)) : QLatin1Char('.'));
        }
        m_asciiReadoutLabel->setText(QStringLiteral("ASCII: \"%1\"").arg(ascii));
    }

    m_updatingViews = false;
}

void ProgrammerCalculatorDialog::reloadStyle() {
    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
    const QString bg = pal.isDark ? QStringLiteral("#141821") : QStringLiteral("#f8fafc");
    const QString cardBg = pal.isDark ? QStringLiteral("#1b202c") : QStringLiteral("#ffffff");
    const QString inputBg = pal.isDark ? QStringLiteral("#0f131a") : QStringLiteral("#ffffff");
    const QString btnBg = pal.isDark ? QStringLiteral("#252b38") : QStringLiteral("#e2e8f0");
    const QString muted = pal.isDark ? QStringLiteral("#94a3b8") : QStringLiteral("#475569");

    setStyleSheet(QStringLiteral(
        "QDialog#ProgrammerCalculatorDialog { background: %1; color: %2; }"
        "QGroupBox { background: %3; border: 1px solid %4; border-radius: 5px; margin-top: 8px; padding-top: 8px; font-weight: 600; color: %2; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 6px; color: %5; }"
        "QLineEdit { background: %6; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 6px; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QLineEdit:focus { border: 1px solid %5; }"
        "QPushButton { background: %7; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 6px; font-size: 11px; font-family: 'JetBrains Mono', monospace; }"
        "QPushButton:hover { border: 1px solid %5; }"
        "QPushButton:checked, QPushButton[bitSet=\"true\"] { background: %5; color: #0b1120; font-weight: bold; border: 1px solid %5; }"
        "QPushButton:disabled { opacity: 0.35; color: %8; }"
        "QLabel#calcBitIndexLabel { color: %8; font-size: 9px; font-family: 'JetBrains Mono', monospace; }"
        "QLabel#calcFloatLabel, QLabel#calcAsciiLabel, QLabel#calcExprStatusLabel { color: %8; font-size: 10px; font-family: 'JetBrains Mono', monospace; }")
                      .arg(bg, pal.textHex, cardBg, pal.borderHex, pal.accentHex, inputBg, btnBg, muted));
}

}  // namespace gridlock::ui
