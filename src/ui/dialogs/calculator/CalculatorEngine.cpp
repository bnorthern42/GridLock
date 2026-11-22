/**
 * @file CalculatorEngine.cpp
 * @brief Implementation of the recursive-descent expression evaluator and bit/IEEE-754 conversion utilities.
 */

#include "CalculatorEngine.hpp"

#include <cmath>
#include <cstring>

namespace gridlock::ui::calc_engine {

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
        : m_input(input),
          m_curValue(currentValue),
          m_curFloat(curFloat),
          m_curIsFloat(curIsFloat),
          m_width(widthBits),
          m_signed(isSigned) {}

    bool parse(EvalResult* outResult, QString* outError) {
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
        if (outResult) {
            outResult->intValue = mask(val.i);
            outResult->floatValue = val.f;
            outResult->isFloat = val.isFloat;
        }
        return true;
    }

private:
    uint64_t mask(uint64_t v) const { return v & widthMask(m_width); }
    int64_t sext(uint64_t v) const { return signExtend(v, m_width); }

    Val makeInt(uint64_t v) const {
        const uint64_t m = mask(v);
        const double d = m_signed ? static_cast<double>(sext(m)) : static_cast<double>(m);
        return Val{m, d, false};
    }

    Val makeFloat(double d) const {
        const int64_t iv = (std::isfinite(d) && d >= static_cast<double>(INT64_MIN) &&
                            d <= static_cast<double>(INT64_MAX))
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
                    const int64_t s = sext(lhs.i);
                    lhs = makeInt((sh >= static_cast<uint64_t>(m_width))
                                      ? (s < 0 ? ~0ULL : 0ULL)
                                      : static_cast<uint64_t>(s >> sh));
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
                        const int64_t a = sext(lhs.i), b = sext(rhs.i);
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
                        const int64_t a = sext(lhs.i), b = sext(rhs.i);
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

bool evaluate(const QString& expr, uint64_t currentValue, int widthBits, bool isSigned,
              double curFloat, bool curIsFloat, EvalResult* outResult, QString* outError) {
    IntExprParser parser(expr, currentValue, widthBits, isSigned, curFloat, curIsFloat);
    return parser.parse(outResult, outError);
}

uint64_t widthMask(int widthBits) {
    if (widthBits == 8) return 0xFFULL;
    if (widthBits == 16) return 0xFFFFULL;
    if (widthBits == 32) return 0xFFFFFFFFULL;
    return 0xFFFFFFFFFFFFFFFFULL;
}

int64_t signExtend(uint64_t val, int widthBits) {
    if (widthBits == 8) return static_cast<int64_t>(static_cast<int8_t>(val & 0xFFULL));
    if (widthBits == 16) return static_cast<int64_t>(static_cast<int16_t>(val & 0xFFFFULL));
    if (widthBits == 32) return static_cast<int64_t>(static_cast<int32_t>(val & 0xFFFFFFFFULL));
    return static_cast<int64_t>(val);
}

uint64_t byteSwap(uint64_t val, int widthBits) {
    uint64_t swapped = val;
    if (widthBits == 64) swapped = __builtin_bswap64(val);
    else if (widthBits == 32) swapped = static_cast<uint64_t>(__builtin_bswap32(static_cast<uint32_t>(val)));
    else if (widthBits == 16) swapped = static_cast<uint64_t>(__builtin_bswap16(static_cast<uint16_t>(val)));
    return swapped & widthMask(widthBits);
}

uint64_t rotateLeft(uint64_t val, int widthBits, int count) {
    const int sh = ((count % widthBits) + widthBits) % widthBits;
    const uint64_t masked = val & widthMask(widthBits);
    if (sh == 0) return masked;
    return ((masked << sh) | (masked >> (widthBits - sh))) & widthMask(widthBits);
}

uint64_t rotateRight(uint64_t val, int widthBits, int count) {
    const int sh = ((count % widthBits) + widthBits) % widthBits;
    const uint64_t masked = val & widthMask(widthBits);
    if (sh == 0) return masked;
    return ((masked >> sh) | (masked << (widthBits - sh))) & widthMask(widthBits);
}

QString formatGroupedBinary(uint64_t val, int widthBits) {
    QString out;
    out.reserve(widthBits + widthBits / 4);
    for (int bit = widthBits - 1; bit >= 0; --bit) {
        out.append(((val >> bit) & 1ULL) ? QLatin1Char('1') : QLatin1Char('0'));
        if (bit > 0 && (bit % 4) == 0) out.append(QLatin1Char(' '));
    }
    return out;
}

QString formatValueForRadix(uint64_t val, int widthBits, int radix, bool isSigned,
                            double floatVal, bool hasFloat) {
    if (radix == 10 && hasFloat) return QString::number(floatVal, 'g', 12);
    const uint64_t masked = val & widthMask(widthBits);
    if (radix == 16) return QStringLiteral("0x%1").arg(QString::number(masked, 16).toUpper());
    if (radix == 8) return QStringLiteral("0o%1").arg(QString::number(masked, 8));
    if (radix == 2) return QStringLiteral("0b%1").arg(QString::number(masked, 2));
    return isSigned ? QString::number(signExtend(masked, widthBits)) : QString::number(masked);
}

QString formatIeee754Readout(uint64_t masked, double floatVal, bool hasFloat) {
    if (hasFloat) {
        const float f32 = static_cast<float>(floatVal);
        const double f64 = floatVal;
        uint32_t bits32 = 0;
        uint64_t bits64 = 0;
        std::memcpy(&bits32, &f32, sizeof(bits32));
        std::memcpy(&bits64, &f64, sizeof(bits64));
        return QStringLiteral("IEEE-754 (%1): float32 = 0x%2  |  float64 = 0x%3")
            .arg(QString::number(floatVal, 'g', 12),
                 QStringLiteral("%1").arg(bits32, 8, 16, QLatin1Char('0')).toUpper(),
                 QStringLiteral("%1").arg(bits64, 16, 16, QLatin1Char('0')).toUpper());
    }
    const uint32_t low32 = static_cast<uint32_t>(masked & 0xFFFFFFFFULL);
    float f32 = 0.0f;
    double f64 = 0.0;
    std::memcpy(&f32, &low32, sizeof(f32));
    std::memcpy(&f64, &masked, sizeof(f64));
    return QStringLiteral("IEEE-754: float = %1  |  double = %2")
        .arg(QString::number(static_cast<double>(f32), 'g', 7),
             QString::number(f64, 'g', 12));
}

QString formatAsciiReadout(uint64_t masked, int widthBits) {
    const int byteCount = widthBits / 8;
    QString ascii;
    ascii.reserve(byteCount);
    for (int i = byteCount - 1; i >= 0; --i) {
        const unsigned char ch = static_cast<unsigned char>((masked >> (i * 8)) & 0xFFU);
        ascii.append((ch >= 0x20 && ch <= 0x7E) ? QChar::fromLatin1(static_cast<char>(ch)) : QLatin1Char('.'));
    }
    return QStringLiteral("ASCII: \"%1\"").arg(ascii);
}

QString normalizeInitialInput(const QString& text) {
    QString s = text.trimmed();
    while (!s.isEmpty() && (s.endsWith(QLatin1Char(';')) || s.endsWith(QLatin1Char(',')))) s.chop(1);
    s = s.trimmed();
    if (s.isEmpty()) return s;
    const int atIdx = s.indexOf(QStringLiteral("@ 0x"), 0, Qt::CaseInsensitive);
    if (atIdx >= 0) return s.mid(atIdx + 2).trimmed().section(QLatin1Char(' '), 0, 0);
    if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) && s.contains(QLatin1Char(' '))) {
        const QString firstTok = s.section(QLatin1Char(' '), 0, 0);
        const QString rest = s.mid(firstTok.size()).trimmed();
        if (rest.startsWith(QLatin1Char('<')) || rest.startsWith(QLatin1Char('[')) || rest.startsWith(QLatin1Char('('))) {
            return firstTok;
        }
    }
    return s;
}

bool parseBaseInput(const QString& raw, int radix, int widthBits, bool isSigned,
                    uint64_t* outValue, bool* outEmpty) {
    QString s = raw.trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
    if (radix == 10) s.remove(QLatin1Char(','));
    else if (radix == 16 && s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) s = s.mid(2);
    else if (radix == 8 && s.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive)) s = s.mid(2);
    else if (radix == 2 && s.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) s = s.mid(2);

    if (s.isEmpty() || (radix == 10 && s == QStringLiteral("-"))) {
        if (outEmpty) *outEmpty = true;
        if (outValue) *outValue = 0ULL;
        return true;
    }
    if (outEmpty) *outEmpty = false;
    bool ok = false;
    const uint64_t parsed = (radix == 10 && (isSigned || s.startsWith(QLatin1Char('-'))))
                                ? static_cast<uint64_t>(s.toLongLong(&ok, 10))
                                : s.toULongLong(&ok, radix);
    if (!ok) return false;
    if (outValue) *outValue = parsed & widthMask(widthBits);
    return true;
}

bool appendDigitToExpression(const QString& currentExpr, QChar digitChar, int inputRadix, QString* outExpr) {
    const QChar upper = digitChar.toUpper();
    int digitVal = -1;
    if (upper >= QLatin1Char('0') && upper <= QLatin1Char('9')) digitVal = upper.unicode() - QLatin1Char('0').unicode();
    else if (upper >= QLatin1Char('A') && upper <= QLatin1Char('F')) digitVal = 10 + (upper.unicode() - QLatin1Char('A').unicode());
    if (digitVal < 0 || digitVal >= inputRadix) return false;

    const bool continuingToken = !currentExpr.isEmpty() && (currentExpr.back().isLetterOrNumber() ||
                                                            currentExpr.back() == QLatin1Char('_') ||
                                                            currentExpr.back() == QLatin1Char('.'));
    QString prefix;
    if (!continuingToken) {
        if (inputRadix == 16) prefix = QStringLiteral("0x");
        else if (inputRadix == 8) prefix = QStringLiteral("0o");
        else if (inputRadix == 2) prefix = QStringLiteral("0b");
    }
    if (outExpr) *outExpr = currentExpr + prefix + upper;
    return true;
}

bool appendDotToExpression(const QString& currentExpr, QString* outExpr) {
    for (int i = currentExpr.size() - 1; i >= 0 && (currentExpr.at(i).isDigit() || currentExpr.at(i) == QLatin1Char('.')); --i) {
        if (currentExpr.at(i) == QLatin1Char('.')) return false;
    }
    if (outExpr) {
        *outExpr = currentExpr + ((!currentExpr.isEmpty() && currentExpr.back().isDigit())
                                      ? QStringLiteral(".")
                                      : QStringLiteral("0."));
    }
    return true;
}

QString backspaceExpressionText(const QString& currentExpr) {
    QString cur = currentExpr;
    if (cur.isEmpty()) return cur;
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
    return cur;
}

QString formatInitialExpressionText(const QString& normalized, uint64_t evaluatedValue, bool isFloat) {
    if (isFloat && !normalized.contains(QLatin1Char(' '))) {
        QString cleanFloat = normalized;
        if (cleanFloat.endsWith(QLatin1Char('f'), Qt::CaseInsensitive) ||
            cleanFloat.endsWith(QLatin1Char('l'), Qt::CaseInsensitive)) {
            cleanFloat.chop(1);
        }
        return cleanFloat;
    }
    if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) && !normalized.contains(QLatin1Char(' '))) {
        return QStringLiteral("0x%1").arg(QString::number(evaluatedValue, 16).toUpper());
    }
    return normalized;
}

}  // namespace gridlock::ui::calc_engine

