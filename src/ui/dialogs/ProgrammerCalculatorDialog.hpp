/**
 * @file ProgrammerCalculatorDialog.hpp
 * @brief Floating Programmer's Calculator dialog for Hex/Dec/Oct/Bin conversion and bit manipulation.
 *
 * Implements ProgrammerCalculatorDialog as an independent floating Qt::Window tool
 * providing synchronized Hexadecimal, Decimal (Signed/Unsigned), Octal, and Binary
 * editing, configurable 8/16/32/64-bit word widths, byte-swap (BSWAP), an interactive
 * 64-bit clickable bit-toggle grid, IEEE-754 float/double and ASCII interpretation,
 * and a built-in C-style integer expression evaluator.
 */

#pragma once

#include <array>
#include <cstdint>

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class ProgrammerCalculatorDialog
 * @brief Non-modal floating programmer's calculator with base editors, 64-bit toggle grid, and expression evaluator.
 */
class ProgrammerCalculatorDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs the floating Programmer's Calculator dialog and loads default width/sign preferences.
     * @param parent Optional owning MainWindow or parent widget.
     */
    explicit ProgrammerCalculatorDialog(QWidget* parent = nullptr);

    /**
     * @brief Virtual destructor.
     */
    ~ProgrammerCalculatorDialog() override = default;

    /**
     * @brief Evaluates a C-style integer expression (supporting hex, binary, octal, decimal, parentheses, bitwise, shifts, and arithmetic).
     * @param expr Expression string (e.g., "(0x10 << 4) | 0x05", "0b1010 ^ 0xff").
     * @param currentValue Current register value substituted for "$" or "ans".
     * @param widthBits Active bit width (8, 16, 32, or 64) used for masking and sign extension.
     * @param isSigned True for signed two's-complement division/right-shift semantics.
     * @param outValue Output pointer receiving the masked result on success.
     * @param outError Optional output pointer receiving a parse/evaluation error description.
     * @return True if @p expr parsed and evaluated cleanly; false otherwise.
     */
    static bool evaluateExpression(const QString& expr, uint64_t currentValue, int widthBits,
                                   bool isSigned, uint64_t* outValue,
                                   QString* outError = nullptr);

    /**
     * @brief Returns the active value masked to the configured word width.
     * @return Masked unsigned 64-bit bit pattern.
     */
    uint64_t value() const { return m_value; }

    /**
     * @brief Sets the active value (masked to the current word width) and synchronizes all base editors and bit buttons.
     * @param val New 64-bit bit pattern.
     */
    void setValue(uint64_t val);

    /**
     * @brief Returns the currently selected word width in bits (8, 16, 32, or 64).
     * @return Active word width.
     */
    int wordWidth() const { return m_wordWidth; }

    /**
     * @brief Changes the active word width (8, 16, 32, or 64), masks upper bits, and refreshes the UI.
     * @param bits Target word width in bits.
     */
    void setWordWidth(int bits);

    /**
     * @brief Returns whether signed two's-complement decimal display and arithmetic shifts are enabled.
     * @return True if signed mode is active; false for unsigned mode.
     */
    bool isSigned() const { return m_signedMode; }

    /**
     * @brief Toggles signed two's-complement vs. unsigned decimal rendering and arithmetic right-shift mode.
     * @param enabled True for signed mode; false for unsigned mode.
     */
    void setSigned(bool enabled);

    /**
     * @brief Reverses the byte order (little-endian <-> big-endian) of the active value for the current word width.
     */
    void byteSwap();

    /**
     * @brief Flips bit @p bitIndex (0..63) when within the active word width and updates all views.
     * @param bitIndex Zero-based bit position (0 = LSB, 63 = MSB).
     */
    void toggleBit(int bitIndex);

    /**
     * @brief Evaluates @p expr using the built-in expression parser and applies the result to the calculator.
     * @param expr C-style integer expression string.
     * @return True if evaluation succeeded and updated the calculator value; false on syntax/math error.
     */
    bool evaluateAndApplyExpression(const QString& expr);

    /**
     * @brief Parses a raw literal (with optional C/C++ U/L/ULL suffixes), negative decimal, or bitwise/arithmetic
     *        expression, loads the resulting value into all synchronized base editors and the 64-bit toggle grid,
     *        and populates #calcExprEdit when an expression was provided.
     * @param text Raw literal, address, register value, or arithmetic expression string.
     */
    void setInitialTextOrValue(const QString& text);

    /** @brief Returns the formatted Hexadecimal text currently shown in #calcHexEdit. */
    QString hexText() const { return m_hexEdit ? m_hexEdit->text() : QString(); }
    /** @brief Returns the formatted Decimal text currently shown in #calcDecEdit. */
    QString decText() const { return m_decEdit ? m_decEdit->text() : QString(); }
    /** @brief Returns the formatted Octal text currently shown in #calcOctEdit. */
    QString octText() const { return m_octEdit ? m_octEdit->text() : QString(); }
    /** @brief Returns the formatted Binary text currently shown in #calcBinEdit. */
    QString binText() const { return m_binEdit ? m_binEdit->text() : QString(); }
    /** @brief Returns the IEEE-754 readout label text currently shown in #calcFloatLabel. */
    QString floatReadoutText() const { return m_floatReadoutLabel ? m_floatReadoutLabel->text() : QString(); }
    /** @brief Returns true when the last evaluated or loaded value was a floating-point number. */
    bool hasFloatValue() const { return m_hasFloatValue; }
    /** @brief Returns the last evaluated floating-point value (or integer value as double). */
    double floatValue() const { return m_hasFloatValue ? m_floatValue : static_cast<double>(m_value); }

    /** @brief Accessor for the Hexadecimal base editor (#calcHexEdit). */
    QLineEdit* hexEdit() const { return m_hexEdit; }
    /** @brief Accessor for the Decimal base editor (#calcDecEdit). */
    QLineEdit* decEdit() const { return m_decEdit; }
    /** @brief Accessor for the Octal base editor (#calcOctEdit). */
    QLineEdit* octEdit() const { return m_octEdit; }
    /** @brief Accessor for the Binary base editor (#calcBinEdit). */
    QLineEdit* binEdit() const { return m_binEdit; }
    /** @brief Accessor for the C-style expression input (#calcExprEdit). */
    QLineEdit* expressionEdit() const { return m_exprEdit; }

    /**
     * @brief Returns the clickable bit-toggle button for 0-based bit position @p bitIndex (0..63).
     * @param bitIndex Zero-based bit index.
     * @return Pointer to QPushButton, or nullptr if out of range.
     */
    QPushButton* bitButton(int bitIndex) const;

    /** @brief Returns the active keypad input radix (10 = DEC, 16 = HEX, 8 = OCT, 2 = BIN). */
    int inputRadix() const { return m_inputRadix; }

    /**
     * @brief Sets the active keypad input radix (10 = DEC, 16 = HEX, 8 = OCT, 2 = BIN) and updates valid digit keycaps.
     * @param radix Target numeric base (10, 16, 8, or 2).
     */
    void setInputRadix(int radix);

    /**
     * @brief Returns the clickable keypad button for hex/decimal digit value @p digitVal (0..15 for '0'..'9','A'..'F').
     */
    QPushButton* keypadDigitButton(int digitVal) const;

    /**
     * @brief Appends a digit ('0'..'9', 'A'..'F') to #calcExprEdit using the active inputRadix() prefix (0x, 0o, 0b, or decimal).
     * @param digitChar Uppercase hex/decimal digit character.
     */
    void pressDigitKey(QChar digitChar);

    /** @brief Appends a decimal point ('.') to #calcExprEdit when in DEC radix mode. */
    void pressDotKey();

    /** @brief Appends the current register value (formatted in the active inputRadix) into #calcExprEdit. */
    void insertCurrentValueToken();

    /** @brief Removes the trailing character from #calcExprEdit. */
    void backspaceExpression();

    /** @brief Clears #calcExprEdit, resets #calcExprStatusLabel, and zeroes the calculator value (AC). */
    void clearAll();

    /**
     * @brief Rotates the active register value left by @p count bits within the active wordWidth().
     */
    void rotateLeft(int count = 1);

    /**
     * @brief Rotates the active register value right by @p count bits within the active wordWidth().
     */
    void rotateRight(int count = 1);

    /**
     * @brief Reloads dialog colors and control styling from the active ThemeManager theme.
     */
    void reloadStyle();

private:
    /** @brief Returns the bitmask corresponding to @p widthBits (8, 16, 32, or 64). */
    static uint64_t widthMask(int widthBits);

    /** @brief Sign-extends @p val from @p widthBits to a signed 64-bit integer. */
    static int64_t signExtend(uint64_t val, int widthBits);

    /** @brief Formats @p val as nibble-grouped binary string for @p widthBits. */
    static QString formatGroupedBinary(uint64_t val, int widthBits);

    /** @brief Formats @p val as a literal token matching @p radix (0x..., 0o..., 0b..., or decimal). */
    QString formatValueForRadix(uint64_t val, int radix) const;

    /** @brief Synchronizes all base QLineEdits, 64 bit-toggle buttons, and IEEE-754/ASCII readouts. */
    void syncAllViews(QLineEdit* skipEditor = nullptr);

    /** @brief Synchronizes radix pill check states and enables/disables 0-9 / A-F digit buttons. */
    void syncKeypadRadixState();

    /** @brief Inserts an operator token into #calcExprEdit (seeding with current register value if empty). */
    void insertOperatorToken(const QString& opToken);

    /** @brief Toggles unary negation (`-`) on the active expression or register value. */
    void toggleNegateExpression();

    uint64_t m_value = 0ULL;
    double m_floatValue = 0.0;
    bool m_hasFloatValue = false;
    int m_wordWidth = 64;
    int m_inputRadix = 10;
    bool m_signedMode = false;
    bool m_updatingViews = false;
    bool m_justEvaluated = false;

    QPushButton* m_width64Btn = nullptr;
    QPushButton* m_width32Btn = nullptr;
    QPushButton* m_width16Btn = nullptr;
    QPushButton* m_width8Btn = nullptr;
    QCheckBox* m_signedCheck = nullptr;
    QPushButton* m_bswapBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QLineEdit* m_hexEdit = nullptr;
    QLineEdit* m_decEdit = nullptr;
    QLineEdit* m_octEdit = nullptr;
    QLineEdit* m_binEdit = nullptr;
    QLabel* m_floatReadoutLabel = nullptr;
    QLabel* m_asciiReadoutLabel = nullptr;

    QLineEdit* m_exprEdit = nullptr;
    QLabel* m_exprStatusLabel = nullptr;
    QPushButton* m_radixDecBtn = nullptr;
    QPushButton* m_radixHexBtn = nullptr;
    QPushButton* m_radixOctBtn = nullptr;
    QPushButton* m_radixBinBtn = nullptr;
    QPushButton* m_dotBtn = nullptr;
    std::array<QPushButton*, 16> m_digitButtons{};
    std::array<QPushButton*, 64> m_bitButtons{};
};

}  // namespace gridlock::ui
