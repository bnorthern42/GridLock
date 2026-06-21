#pragma once
#include <QApplication>
#include <QString>

namespace gridlock::ui {

/**
 * ThemeManager — singleton that owns the global Qt stylesheet.
 *
 * All colour tokens live here.  Widget-level setStyleSheet() calls
 * should be limited to structural concerns only (margins, fonts).
 * Semantic targeting is done via objectNames set on widgets and
 * matched in the stylesheet below.
 */
class ThemeManager {
public:
    static ThemeManager& instance() {
        static ThemeManager inst;
        return inst;
    }

    // Apply the full global stylesheet to the application.
    void applyGlobalTheme(QApplication& app) const;

    // Colour tokens (for code that needs to paint manually, e.g. ServerRackView).
    static constexpr const char* BASE       = "#1e1e2e";
    static constexpr const char* MANTLE     = "#181825";
    static constexpr const char* SURFACE0   = "#313244";
    static constexpr const char* SURFACE1   = "#45475a";
    static constexpr const char* OVERLAY1   = "#7f849c";
    static constexpr const char* TEXT       = "#cdd6f4";
    static constexpr const char* SUBTEXT    = "#a6adc8";
    static constexpr const char* LAVENDER   = "#b4befe";
    static constexpr const char* BLUE       = "#89b4fa";
    static constexpr const char* MAUVE      = "#cba6f7";
    static constexpr const char* GREEN      = "#a6e3a1";
    static constexpr const char* YELLOW     = "#f9e2af";
    static constexpr const char* RED        = "#f38ba8";
    static constexpr const char* HOVER_BG   = "#252535";

    // Returns the full compiled QSS string.
    static QString stylesheet();

private:
    ThemeManager() = default;
};

} // namespace gridlock::ui
