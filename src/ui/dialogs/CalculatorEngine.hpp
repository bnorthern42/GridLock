/**
 * @file CalculatorEngine.hpp
 * @brief Expression evaluator and IEEE-754 / endianness / bit-conversion helpers for ProgrammerCalculatorDialog.
 *
 * Isolates pure numeric parsing, recursive-descent C-style integer/floating-point
 * expression evaluation, word-width masking, sign extension, byte swapping, bitwise
 * rotation, and IEEE-754/ASCII formatting from Qt widget layout code.
 */

#pragma once

#include <cstdint>

#include <QString>

namespace gridlock::ui::calc_engine {

/**
 * @struct EvalResult
 * @brief Result payload produced by evaluating a C-style integer or floating-point expression.
 */
struct EvalResult {
    uint64_t intValue = 0ULL;  ///< Masked unsigned bit pattern for the configured word width.
    double floatValue = 0.0;   ///< Floating-point representation of the evaluated result.
    bool isFloat = false;      ///< True when the expression promoted to IEEE-754 floating-point math.
};

/**
 * @brief Evaluates a C-style integer or floating-point expression.
 * @param expr Expression string (e.g., "(0x10 << 4) | 0x05", "3.3 * 2", "0b1010 ^ 0xff").
 * @param currentValue Current register value substituted for "$" or "ans".
 * @param widthBits Active bit width (8, 16, 32, or 64) used for masking and sign extension.
 * @param isSigned True for signed two's-complement division/right-shift semantics.
 * @param curFloat Current floating-point value substituted when @p curIsFloat is true.
 * @param curIsFloat True when the active register currently holds a floating-point value.
 * @param outResult Output pointer receiving evaluated integer/float fields on success.
 * @param outError Optional output pointer receiving a parse/evaluation error description.
 * @return True if @p expr parsed and evaluated cleanly; false otherwise.
 */
bool evaluate(const QString& expr, uint64_t currentValue, int widthBits, bool isSigned,
              double curFloat, bool curIsFloat, EvalResult* outResult,
              QString* outError = nullptr);

/** @brief Returns the bitmask corresponding to @p widthBits (8, 16, 32, or 64). */
uint64_t widthMask(int widthBits);

/** @brief Sign-extends @p val from @p widthBits to a signed 64-bit integer. */
int64_t signExtend(uint64_t val, int widthBits);

/** @brief Reverses byte order (BSWAP) of @p val within @p widthBits (16, 32, or 64). */
uint64_t byteSwap(uint64_t val, int widthBits);

/** @brief Rotates @p val left by @p count bits within @p widthBits. */
uint64_t rotateLeft(uint64_t val, int widthBits, int count);

/** @brief Rotates @p val right by @p count bits within @p widthBits. */
uint64_t rotateRight(uint64_t val, int widthBits, int count);

/** @brief Formats @p val as a nibble-grouped binary string for @p widthBits. */
QString formatGroupedBinary(uint64_t val, int widthBits);

/** @brief Formats @p val as a prefixed literal matching @p radix (0x..., 0o..., 0b..., or decimal). */
QString formatValueForRadix(uint64_t val, int widthBits, int radix, bool isSigned,
                            double floatVal, bool hasFloat);

/** @brief Formats the IEEE-754 float32/float64 readout string for the active register or float value. */
QString formatIeee754Readout(uint64_t masked, double floatVal, bool hasFloat);

/** @brief Formats the printable ASCII byte readout string for @p masked across @p widthBits. */
QString formatAsciiReadout(uint64_t masked, int widthBits);

/**
 * @brief Strips trailing punctuation, debugger address annotations (`@ 0x...`), and symbol tags from raw input.
 * @param text Raw text sent from an editor, variable view, or register view.
 * @return Normalized expression or literal string ready for evaluation.
 */
QString normalizeInitialInput(const QString& text);

/**
 * @brief Parses a live base-editor string (HEX/DEC/OCT/BIN) into a masked word-width value.
 * @param raw Raw QLineEdit text.
 * @param radix Base (16, 10, 8, or 2).
 * @param widthBits Active word width in bits.
 * @param isSigned True if signed decimal mode is enabled.
 * @param outValue Receives the parsed, masked value on success (or 0 when @p outEmpty is true).
 * @param outEmpty Optional flag set true when @p raw is blank (or a lone "-" in DEC mode).
 * @return True if @p raw is empty or parsed cleanly as an integer in @p radix.
 */
bool parseBaseInput(const QString& raw, int radix, int widthBits, bool isSigned,
                    uint64_t* outValue, bool* outEmpty = nullptr);

/** @brief Appends a validated radix digit with auto-prefix (0x/0o/0b) to @p currentExpr. */
bool appendDigitToExpression(const QString& currentExpr, QChar digitChar, int inputRadix, QString* outExpr);

/** @brief Appends a decimal point to @p currentExpr if valid in decimal input mode. */
bool appendDotToExpression(const QString& currentExpr, QString* outExpr);

/** @brief Removes the trailing character, spaced operator, or radix prefix from @p currentExpr. */
QString backspaceExpressionText(const QString& currentExpr);

/** @brief Formats a normalized initial input string for display inside the expression editor. */
QString formatInitialExpressionText(const QString& normalized, uint64_t evaluatedValue, bool isFloat);

}  // namespace gridlock::ui::calc_engine

