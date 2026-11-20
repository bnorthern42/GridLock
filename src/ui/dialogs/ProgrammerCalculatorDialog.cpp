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
 * @brief Self-contained recursive-descent parser for C-style 64-bit integer and floating-point expressions.
 *
 * Supports decimal, float/double (3.3, 3.3f, 2.5e-3), hexadecimal (0x), binary (0b), octal (0o),
 * '$'/'ans' current value, parentheses, unary (+, -, ~, !), multiplicative (*, /, %),
 * additive (+, -), shift (<<, >>), and bitwise (&, ^, |) operators.
 */
class IntExprParser {
public:
    struct Val {
        uint64_t i = 0ULL;
        double f = 0.0;
        bool isFloat = false;
    };

    IntExprParser(const QString& input, uint64_t currentValue, int widthBits, bool isSigned,
                  double curFloat = 0.0, bool curIsFloat = false)
        : m_input(input), m_curValue(currentValue), m_curFloat(curFloat), m_curIsFloat(curIsFloat),
          m_width(widthBits), m_signed(isSigned) {}

    bool parse(uint64_t* outValue, QString* outError, double* outFloat = nullptr, bool* outIsFloat = nullptr) {
        skipWs();
        if (m_pos >= m_input.size()) {
            if (outError) *outError = QStringLiteral("Empty expression");
            return false;
        }
        const Val val = parseBitOr();
        skipWs();
        if (!m_error.isEmpty()) {
            if (outError) *outError = m_error;
            return false;
        }
        if (m_pos < m_input.size()) {
            if (outError) *outError = QStringLiteral("Unexpected token at position %1").arg(m_pos + 1);
            return false;
        }
        if (outValue) *outValue = mask(val.i);
        if (outFloat) *outFloat = val.f;
        if (outIsFloat) *outIsFloat = val.isFloat;
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

    Val makeInt(uint64_t v) const {
        const uint64_t m = mask(v);
        const double d = m_signed ? static_cast<double>(signExtend(m)) : static_cast<double>(m);
        return Val{m, d, false};
    }

    Val makeFloat(double d) const {
        const int64_t iv = (std::isfinite(d) && d >= static_cast<double>(INT64_MIN) && d <= static_cast<double>(INT64_MAX))
                               ? static_cast<int64_t>(d)
                               : 0;
        return Val{mask(static_cast<uint64_t>(iv)), d, true};
    }

    void skipWs() { while (m_pos < m_input.size() && m_input.at(m_pos).isSpace()) ++m_pos; }

    bool match(const QString& tok) {
        skipWs();
        if (m_input.mid(m_pos, tok.size()) == tok) { m_pos += tok.size(); return true; }
        return false;
    }

    Val parseBitOr() {
        Val lhs = parseBitXor();
        while (m_error.isEmpty() && match(QStringLiteral("|"))) lhs = makeInt(lhs.i | parseBitXor().i);
        return lhs;
    }

    Val parseBitXor() {
        Val lhs = parseBitAnd();
        while (m_error.isEmpty() && match(QStringLiteral("^"))) lhs = makeInt(lhs.i ^ parseBitAnd().i);
        return lhs;
    }

    Val parseBitAnd() {
        Val lhs = parseShift();
        while (m_error.isEmpty() && match(QStringLiteral("&"))) lhs = makeInt(lhs.i & parseShift().i);
        return lhs;
    }

    Val parseShift() {
        Val lhs = parseAddSub();
        while (m_error.isEmpty()) {
            if (match(QStringLiteral("<<"))) {
                const uint64_t sh = parseAddSub().i;
                lhs = makeInt((sh >= static_cast<uint64_t>(m_width)) ? 0ULL : (lhs.i << sh));
            } else if (match(QStringLiteral(">>"))) {
                const uint64_t sh = parseAddSub().i;
                if (m_signed) {
                    const int64_t s = signExtend(lhs.i);
                    lhs = makeInt((sh >= static_cast<uint64_t>(m_width)) ? (s < 0 ? ~0ULL : 0ULL) : static_cast<uint64_t>(s >> sh));
                } else {
                    lhs = makeInt((sh >= static_cast<uint64_t>(m_width)) ? 0ULL : (mask(lhs.i) >> sh));
                }
            } else {
                break;
            }
        }
        return lhs;
    }

    Val parseAddSub() {
        Val lhs = parseMulDiv();
        while (m_error.isEmpty()) {
            if (match(QStringLiteral("+"))) {
                const Val rhs = parseMulDiv();
                lhs = (lhs.isFloat || rhs.isFloat) ? makeFloat(lhs.f + rhs.f) : makeInt(lhs.i + rhs.i);
            } else if (match(QStringLiteral("-"))) {
                const Val rhs = parseMulDiv();
                lhs = (lhs.isFloat || rhs.isFloat) ? makeFloat(lhs.f - rhs.f) : makeInt(lhs.i - rhs.i);
            } else {
                break;
            }
        }
        return lhs;
    }

    Val parseMulDiv() {
        Val lhs = parseUnary();
        while (m_error.isEmpty()) {
            if (match(QStringLiteral("*"))) {
                const Val rhs = parseUnary();
                lhs = (lhs.isFloat || rhs.isFloat) ? makeFloat(lhs.f * rhs.f) : makeInt(lhs.i * rhs.i);
            } else if (match(QStringLiteral("/"))) {
                const Val rhs = parseUnary();
                if (lhs.isFloat || rhs.isFloat) {
                    if (rhs.f == 0.0) { m_error = QStringLiteral("Division by zero"); return makeFloat(0.0); }
                    lhs = makeFloat(lhs.f / rhs.f);
                } else {
                    if (rhs.i == 0ULL) { m_error = QStringLiteral("Division by zero"); return makeInt(0ULL); }
                    if (m_signed) {
                        const int64_t a = signExtend(lhs.i), b = signExtend(rhs.i);
                        lhs = makeInt((b == -1 && a == INT64_MIN) ? static_cast<uint64_t>(a) : static_cast<uint64_t>(a / b));
                    } else {
                        lhs = makeInt(mask(lhs.i) / mask(rhs.i));
                    }
                }
            } else if (match(QStringLiteral("%"))) {
                const Val rhs = parseUnary();
                if (lhs.isFloat || rhs.isFloat) {
                    if (rhs.f == 0.0) { m_error = QStringLiteral("Modulo by zero"); return makeFloat(0.0); }
                    lhs = makeFloat(std::fmod(lhs.f, rhs.f));
                } else {
                    if (rhs.i == 0ULL) { m_error = QStringLiteral("Modulo by zero"); return makeInt(0ULL); }
                    if (m_signed) {
                        const int64_t a = signExtend(lhs.i), b = signExtend(rhs.i);
                        lhs = makeInt((b == -1 && a == INT64_MIN) ? 0ULL : static_cast<uint64_t>(a % b));
                    } else {
                        lhs = makeInt(mask(lhs.i) % mask(rhs.i));
                    }
                }
            } else {
                break;
            }
        }
        return lhs;
    }

    Val parseUnary() {
        skipWs();
        if (match(QStringLiteral("+"))) return parseUnary();
        if (match(QStringLiteral("-"))) {
            const Val v = parseUnary();
            return v.isFloat ? makeFloat(-v.f) : makeInt(0ULL - v.i);
        }
        if (match(QStringLiteral("~"))) return makeInt(~parseUnary().i);
        if (match(QStringLiteral("!"))) return makeInt((mask(parseUnary().i) == 0ULL) ? 1ULL : 0ULL);
        return parsePrimary();
    }

    Val parsePrimary() {
        skipWs();
        if (match(QStringLiteral("("))) {
            const Val v = parseBitOr();
            if (!match(QStringLiteral(")"))) m_error = QStringLiteral("Missing closing ')'");
            return v;
        }
        if (match(QStringLiteral("$"))) return m_curIsFloat ? makeFloat(m_curFloat) : makeInt(m_curValue);
        if (m_input.mid(m_pos, 3).compare(QStringLiteral("ans"), Qt::CaseInsensitive) == 0 &&
            (m_pos + 3 >= m_input.size() || !m_input.at(m_pos + 3).isLetterOrNumber())) {
            m_pos += 3;
            return m_curIsFloat ? makeFloat(m_curFloat) : makeInt(m_curValue);
        }

        const int start = m_pos;
        const bool isHex = m_input.mid(m_pos, 2).compare(QStringLiteral("0x"), Qt::CaseInsensitive) == 0;
        while (m_pos < m_input.size()) {
            const QChar c = m_input.at(m_pos);
            if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('\'') || (!isHex && c == QLatin1Char('.'))) {
                ++m_pos;
                if (!isHex && (c == QLatin1Char('e') || c == QLatin1Char('E')) && m_pos < m_input.size() &&
                    (m_input.at(m_pos) == QLatin1Char('+') || m_input.at(m_pos) == QLatin1Char('-'))) {
                    ++m_pos;
                }
            } else {
                break;
            }
        }
        if (m_pos == start) {
            m_error = QStringLiteral("Expected number at position %1").arg(m_pos + 1);
            return makeInt(0ULL);
        }

        QString tok = m_input.mid(start, m_pos - start).remove(QLatin1Char('_')).remove(QLatin1Char('\''));
        if (!isHex && !tok.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive) &&
            !tok.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive) &&
            (tok.contains(QLatin1Char('.')) || tok.contains(QLatin1Char('e'), Qt::CaseInsensitive) ||
             tok.endsWith(QLatin1Char('f'), Qt::CaseInsensitive))) {
            if (tok.endsWith(QLatin1Char('f'), Qt::CaseInsensitive) || tok.endsWith(QLatin1Char('l'), Qt::CaseInsensitive)) {
                tok.chop(1);
            }
            bool ok = false;
            const double d = tok.toDouble(&ok);
            if (!ok) { m_error = QStringLiteral("Invalid floating-point literal '%1'").arg(tok); return makeFloat(0.0); }
            return makeFloat(d);
        }

        int base = 10;
        if (tok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) { base = 16; tok = tok.mid(2); }
        else if (tok.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) { base = 2; tok = tok.mid(2); }
        else if (tok.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive)) { base = 8; tok = tok.mid(2); }
        else if (tok.size() > 1 && tok.startsWith(QLatin1Char('0')) && tok.at(1).isDigit()) { base = 8; tok = tok.mid(1); }
        while (tok.size() > 1 && (tok.back().toLower() == QLatin1Char('u') || tok.back().toLower() == QLatin1Char('l'))) {
            tok.chop(1);
        }
        bool ok = false;
        const uint64_t val = tok.toULongLong(&ok, base);
        if (!ok) {
            m_error = QStringLiteral("Invalid integer literal '%1'").arg(tok);
            return makeInt(0ULL);
        }
        return makeInt(val);
    }

    QString m_input;
    int m_pos = 0;
    uint64_t m_curValue = 0ULL;
    double m_curFloat = 0.0;
    bool m_curIsFloat = false;
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

    auto addBaseRow = [&](int row, const QString& label, const QString& editObj, const QString& copyObj, QLineEdit** outEdit) {
        auto* lbl = new QLabel(label, basesGroup); lbl->setFixedWidth(48);
        auto* ed = new QLineEdit(basesGroup); ed->setObjectName(editObj); ed->setFixedHeight(26);
        auto* copyBtn = new QPushButton(QStringLiteral("Copy"), basesGroup);
        copyBtn->setObjectName(copyObj); copyBtn->setFixedSize(56, 26);
        connect(copyBtn, &QPushButton::clicked, this, [ed]() {
            if (QClipboard* cb = QApplication::clipboard()) cb->setText(ed->text());
        });
        basesGrid->addWidget(lbl, row, 0); basesGrid->addWidget(ed, row, 1); basesGrid->addWidget(copyBtn, row, 2);
        *outEdit = ed;
    };
    addBaseRow(0, QStringLiteral("HEX"), QStringLiteral("calcHexEdit"), QStringLiteral("calcHexCopyBtn"), &m_hexEdit);
    addBaseRow(1, QStringLiteral("DEC"), QStringLiteral("calcDecEdit"), QStringLiteral("calcDecCopyBtn"), &m_decEdit);
    addBaseRow(2, QStringLiteral("OCT"), QStringLiteral("calcOctEdit"), QStringLiteral("calcOctCopyBtn"), &m_octEdit);
    addBaseRow(3, QStringLiteral("BIN"), QStringLiteral("calcBinEdit"), QStringLiteral("calcBinCopyBtn"), &m_binEdit);

    // IEEE-754 Float/Double and ASCII byte interpretation row.
    m_floatReadoutLabel = new QLabel(basesGroup); m_floatReadoutLabel->setObjectName(QStringLiteral("calcFloatLabel"));
    m_asciiReadoutLabel = new QLabel(basesGroup); m_asciiReadoutLabel->setObjectName(QStringLiteral("calcAsciiLabel"));
    auto* metaRow = new QHBoxLayout();
    metaRow->addWidget(m_floatReadoutLabel, 1); metaRow->addWidget(m_asciiReadoutLabel);
    basesGrid->addLayout(metaRow, 4, 0, 1, 3);
    root->addWidget(basesGroup);

    // Connect base editors so editing any base immediately updates all other bases and the bit grid.
    connect(m_hexEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
        if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) s = s.mid(2);
        if (s.isEmpty()) { m_hasFloatValue = false; m_value = 0ULL; syncAllViews(m_hexEdit); return; }
        bool ok = false; const uint64_t parsed = s.toULongLong(&ok, 16);
        if (ok) { m_hasFloatValue = false; m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_hexEdit); }
    });
    connect(m_decEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_')).remove(QLatin1Char(','));
        if (s.isEmpty() || s == QStringLiteral("-")) { m_hasFloatValue = false; m_value = 0ULL; syncAllViews(m_decEdit); return; }
        bool ok = false;
        const uint64_t parsed = (m_signedMode || s.startsWith(QLatin1Char('-')))
                                    ? static_cast<uint64_t>(s.toLongLong(&ok, 10))
                                    : s.toULongLong(&ok, 10);
        if (ok) { m_hasFloatValue = false; m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_decEdit); }
    });
    connect(m_octEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
        if (s.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive)) s = s.mid(2);
        if (s.isEmpty()) { m_hasFloatValue = false; m_value = 0ULL; syncAllViews(m_octEdit); return; }
        bool ok = false; const uint64_t parsed = s.toULongLong(&ok, 8);
        if (ok) { m_hasFloatValue = false; m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_octEdit); }
    });
    connect(m_binEdit, &QLineEdit::textChanged, this, [this](const QString& raw) {
        if (m_updatingViews) return;
        QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
        if (s.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) s = s.mid(2);
        if (s.isEmpty()) { m_hasFloatValue = false; m_value = 0ULL; syncAllViews(m_binEdit); return; }
        bool ok = false; const uint64_t parsed = s.toULongLong(&ok, 2);
        if (ok) { m_hasFloatValue = false; m_value = parsed & widthMask(m_wordWidth); syncAllViews(m_binEdit); }
    });

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

    // Full tactile 5x8 Programmer's Calculator Button Grid.
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

    // Row 1: C, D, 4, 5, 6, XOR, NOT, ⌫
    makeDigitBtn(12, QStringLiteral("C"), 1, 0); makeDigitBtn(13, QStringLiteral("D"), 1, 1);
    makeDigitBtn(4, QStringLiteral("4"), 1, 2); makeDigitBtn(5, QStringLiteral("5"), 1, 3); makeDigitBtn(6, QStringLiteral("6"), 1, 4);
    makePadBtn(QStringLiteral("XOR"), QStringLiteral("calcOpXorBtn"), QStringLiteral("op"), 1, 5, 1, [this]() { insertOperatorToken(QStringLiteral(" ^ ")); });
    makePadBtn(QStringLiteral("NOT"), QStringLiteral("calcOpNotBtn"), QStringLiteral("op"), 1, 6, 1, [this]() {
        if (m_exprEdit && m_exprEdit->text().trimmed().isEmpty()) { setValue(~m_value); }
        else if (m_exprEdit) { m_exprEdit->insert(QStringLiteral("~")); m_exprEdit->setFocus(); }
    });
    makePadBtn(QStringLiteral("⌫"), QStringLiteral("calcKeyBackspace"), QStringLiteral("clear"), 1, 7, 1, [this]() { backspaceExpression(); });

    // Row 2: E, F, 1, 2, 3, <<, >>, (
    makeDigitBtn(14, QStringLiteral("E"), 2, 0); makeDigitBtn(15, QStringLiteral("F"), 2, 1);
    makeDigitBtn(1, QStringLiteral("1"), 2, 2); makeDigitBtn(2, QStringLiteral("2"), 2, 3); makeDigitBtn(3, QStringLiteral("3"), 2, 4);
    makePadBtn(QStringLiteral("<<"), QStringLiteral("calcOpShlBtn"), QStringLiteral("op"), 2, 5, 1, [this]() { insertOperatorToken(QStringLiteral(" << ")); });
    makePadBtn(QStringLiteral(">>"), QStringLiteral("calcOpShrBtn"), QStringLiteral("op"), 2, 6, 1, [this]() { insertOperatorToken(QStringLiteral(" >> ")); });
    makePadBtn(QStringLiteral("("), QStringLiteral("calcKeyLParen"), QStringLiteral("op"), 2, 7, 1, [this]() {
        if (m_exprEdit) {
            if (m_justEvaluated) { m_exprEdit->clear(); m_justEvaluated = false; }
            m_exprEdit->insert(QStringLiteral("(")); m_exprEdit->setFocus();
        }
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

    // Row 4: status label on left (cols 0..2), %, -, +, and wide highlighted = (cols 6..7)
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
    if (m_wordWidth == 64) m_value = __builtin_bswap64(m_value);
    else if (m_wordWidth == 32) m_value = static_cast<uint64_t>(__builtin_bswap32(static_cast<uint32_t>(m_value)));
    else if (m_wordWidth == 16) m_value = static_cast<uint64_t>(__builtin_bswap16(static_cast<uint16_t>(m_value)));
    m_value &= widthMask(m_wordWidth); syncAllViews();
}

void ProgrammerCalculatorDialog::rotateLeft(int count) {
    const int sh = ((count % m_wordWidth) + m_wordWidth) % m_wordWidth;
    if (sh == 0) return;
    m_hasFloatValue = false;
    const uint64_t masked = m_value & widthMask(m_wordWidth);
    m_value = ((masked << sh) | (masked >> (m_wordWidth - sh))) & widthMask(m_wordWidth); syncAllViews();
}

void ProgrammerCalculatorDialog::rotateRight(int count) {
    const int sh = ((count % m_wordWidth) + m_wordWidth) % m_wordWidth;
    if (sh == 0) return;
    m_hasFloatValue = false;
    const uint64_t masked = m_value & widthMask(m_wordWidth);
    m_value = ((masked >> sh) | (masked << (m_wordWidth - sh))) & widthMask(m_wordWidth); syncAllViews();
}

void ProgrammerCalculatorDialog::toggleBit(int bitIndex) {
    if (bitIndex < 0 || bitIndex >= m_wordWidth) return;
    m_hasFloatValue = false;
    m_value = (m_value ^ (1ULL << bitIndex)) & widthMask(m_wordWidth); syncAllViews();
}

bool ProgrammerCalculatorDialog::evaluateAndApplyExpression(const QString& expr) {
    uint64_t result = 0ULL; double fResult = 0.0; bool isFloat = false; QString err;
    IntExprParser parser(expr, m_value, m_wordWidth, m_signedMode, m_floatValue, m_hasFloatValue);
    if (!parser.parse(&result, &err, &fResult, &isFloat)) {
        if (m_exprStatusLabel) m_exprStatusLabel->setText(QStringLiteral("Error: %1").arg(err));
        return false;
    }
    m_value = result & widthMask(m_wordWidth);
    m_floatValue = fResult;
    m_hasFloatValue = isFloat;
    m_justEvaluated = true;
    if (isFloat && fResult < 0.0 && !m_signedMode) setSigned(true);
    if (m_exprStatusLabel) {
        if (isFloat) {
            m_exprStatusLabel->setText(QStringLiteral("Result = %1 (int: %2, 0x%3)")
                .arg(QString::number(fResult, 'g', 12),
                     m_signedMode ? QString::number(signExtend(m_value, m_wordWidth)) : QString::number(m_value),
                     QString::number(m_value, 16).toUpper()));
        } else {
            m_exprStatusLabel->setText(QStringLiteral("Result = 0x%1 (%2)")
                .arg(QString::number(m_value, 16).toUpper(),
                     m_signedMode ? QString::number(signExtend(m_value, m_wordWidth)) : QString::number(m_value)));
        }
    }
    syncAllViews(); return true;
}

void ProgrammerCalculatorDialog::setInitialTextOrValue(const QString& text) {
    QString s = text.trimmed();
    while (!s.isEmpty() && (s.endsWith(QLatin1Char(';')) || s.endsWith(QLatin1Char(',')))) s.chop(1);
    s = s.trimmed();
    if (s.isEmpty()) return;
    const int atIdx = s.indexOf(QStringLiteral("@ 0x"), 0, Qt::CaseInsensitive);
    if (atIdx >= 0) s = s.mid(atIdx + 2).trimmed().section(QLatin1Char(' '), 0, 0);
    else if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) && s.contains(QLatin1Char(' '))) {
        const QString firstTok = s.section(QLatin1Char(' '), 0, 0);
        const QString rest = s.mid(firstTok.size()).trimmed();
        if (rest.startsWith(QLatin1Char('<')) || rest.startsWith(QLatin1Char('[')) || rest.startsWith(QLatin1Char('('))) s = firstTok;
    }
    if (s.startsWith(QLatin1Char('-')) && !s.mid(1).trimmed().contains(QLatin1Char(' '))) setSigned(true);
    uint64_t result = 0ULL; double fResult = 0.0; bool isFloat = false; QString err;
    IntExprParser parser(s, m_value, m_wordWidth, m_signedMode, m_floatValue, m_hasFloatValue);
    if (parser.parse(&result, &err, &fResult, &isFloat)) {
        m_value = result & widthMask(m_wordWidth);
        m_floatValue = fResult;
        m_hasFloatValue = isFloat;
        m_justEvaluated = true;
        if (isFloat && fResult < 0.0 && !m_signedMode) setSigned(true);
        if (m_exprStatusLabel) m_exprStatusLabel->clear();
        if (m_exprEdit) {
            if (isFloat && !s.contains(QLatin1Char(' '))) {
                QString cleanFloat = s;
                if (cleanFloat.endsWith(QLatin1Char('f'), Qt::CaseInsensitive) ||
                    cleanFloat.endsWith(QLatin1Char('l'), Qt::CaseInsensitive)) {
                    cleanFloat.chop(1);
                }
                m_exprEdit->setText(cleanFloat);
            } else if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) && !s.contains(QLatin1Char(' '))) {
                m_exprEdit->setText(QStringLiteral("0x%1").arg(QString::number(m_value, 16).toUpper()));
            } else {
                m_exprEdit->setText(s);
            }
        }
        syncAllViews(); return;
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
    if (radix == 10 && m_hasFloatValue) return QString::number(m_floatValue, 'g', 12);
    const uint64_t masked = val & widthMask(m_wordWidth);
    if (radix == 16) return QStringLiteral("0x%1").arg(QString::number(masked, 16).toUpper());
    if (radix == 8) return QStringLiteral("0o%1").arg(QString::number(masked, 8));
    if (radix == 2) return QStringLiteral("0b%1").arg(QString::number(masked, 2));
    return m_signedMode ? QString::number(signExtend(masked, m_wordWidth)) : QString::number(masked);
}

void ProgrammerCalculatorDialog::pressDigitKey(QChar digitChar) {
    if (!m_exprEdit) return;
    const QChar upper = digitChar.toUpper();
    int digitVal = -1;
    if (upper >= QLatin1Char('0') && upper <= QLatin1Char('9')) digitVal = upper.unicode() - QLatin1Char('0').unicode();
    else if (upper >= QLatin1Char('A') && upper <= QLatin1Char('F')) digitVal = 10 + (upper.unicode() - QLatin1Char('A').unicode());
    if (digitVal < 0 || digitVal >= m_inputRadix) return;

    if (m_justEvaluated) { m_exprEdit->clear(); m_justEvaluated = false; }
    const QString cur = m_exprEdit->text();
    const bool continuingToken = !cur.isEmpty() && (cur.back().isLetterOrNumber() || cur.back() == QLatin1Char('_') || cur.back() == QLatin1Char('.'));
    QString prefix;
    if (!continuingToken) {
        if (m_inputRadix == 16) prefix = QStringLiteral("0x");
        else if (m_inputRadix == 8) prefix = QStringLiteral("0o");
        else if (m_inputRadix == 2) prefix = QStringLiteral("0b");
    }
    m_exprEdit->setText(cur + prefix + upper);
    m_exprEdit->setFocus();
}

void ProgrammerCalculatorDialog::pressDotKey() {
    if (!m_exprEdit || m_inputRadix != 10) return;
    if (m_justEvaluated) { m_exprEdit->clear(); m_justEvaluated = false; }
    const QString cur = m_exprEdit->text();
    int i = cur.size() - 1;
    while (i >= 0 && (cur.at(i).isDigit() || cur.at(i) == QLatin1Char('.'))) {
        if (cur.at(i) == QLatin1Char('.')) return;
        --i;
    }
    const bool afterDigit = !cur.isEmpty() && cur.back().isDigit();
    m_exprEdit->setText(cur + (afterDigit ? QStringLiteral(".") : QStringLiteral("0.")));
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
    QString cur = m_exprEdit->text();
    if (cur.isEmpty()) return;
    if (cur.endsWith(QStringLiteral(" << ")) || cur.endsWith(QStringLiteral(" >> "))) {
        cur.chop(4);
    } else if (cur.size() >= 3 && cur.endsWith(QLatin1Char(' ')) && cur.at(cur.size() - 3) == QLatin1Char(' ')) {
        cur.chop(3);
    } else {
        cur.chop(1);
        if (cur.endsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ||
            cur.endsWith(QStringLiteral("0o"), Qt::CaseInsensitive) ||
            cur.endsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) {
            if (cur.size() == 2 || !cur.at(cur.size() - 3).isLetterOrNumber()) cur.chop(2);
        }
    }
    m_exprEdit->setText(cur);
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

    // Update interactive 64-bit button states and active width enablement.
    for (int b = 0; b < 64; ++b) {
        QPushButton* btn = m_bitButtons[static_cast<size_t>(b)];
        if (!btn) continue;
        const bool inRange = (b < m_wordWidth), bitSet = inRange && (((masked >> b) & 1ULL) != 0ULL);
        btn->setEnabled(inRange); btn->setText(bitSet ? QStringLiteral("1") : QStringLiteral("0"));
        btn->setProperty("bitSet", bitSet); btn->style()->unpolish(btn); btn->style()->polish(btn);
    }

    // Update IEEE-754 float/double readout (shows exact IEEE-754 hex bit patterns when a float is active).
    if (m_floatReadoutLabel) {
        if (m_hasFloatValue) {
            const float f32 = static_cast<float>(m_floatValue);
            const double f64 = m_floatValue;
            uint32_t bits32 = 0;
            uint64_t bits64 = 0;
            std::memcpy(&bits32, &f32, sizeof(bits32));
            std::memcpy(&bits64, &f64, sizeof(bits64));
            m_floatReadoutLabel->setText(
                QStringLiteral("IEEE-754 (%1): float32 = 0x%2  |  float64 = 0x%3")
                    .arg(QString::number(m_floatValue, 'g', 12),
                         QStringLiteral("%1").arg(bits32, 8, 16, QLatin1Char('0')).toUpper(),
                         QStringLiteral("%1").arg(bits64, 16, 16, QLatin1Char('0')).toUpper()));
        } else {
            const uint32_t low32 = static_cast<uint32_t>(masked & 0xFFFFFFFFULL);
            float f32 = 0.0f;
            double f64 = 0.0;
            std::memcpy(&f32, &low32, sizeof(f32));
            std::memcpy(&f64, &masked, sizeof(f64));
            m_floatReadoutLabel->setText(
                QStringLiteral("IEEE-754: float = %1  |  double = %2")
                    .arg(QString::number(static_cast<double>(f32), 'g', 7),
                         QString::number(f64, 'g', 12)));
        }
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
