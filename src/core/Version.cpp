/**
 * @file Version.cpp
 * @brief Implementation of semantic version string and component accessors.
 *
 * Parses the compile-time GRIDLOCK_VERSION_STRING macro defined in
 * VersionConfig.hpp into individual integer version fields.
 */

#include "Version.hpp"

#include <QStringList>

#include "VersionConfig.hpp"

namespace gridlock::core {

QString Version::getString() {
    // Return the compile-time version literal injected by the Meson build system.
    return QStringLiteral(GRIDLOCK_VERSION_STRING);
}

int Version::getMajor() {
    // Split "MAJOR.MINOR.PATCH" on '.' and parse the leading major token.
    const QStringList parts = getString().split('.');
    if (parts.size() > 0) {
        return parts[0].toInt();
    }
    return 0;
}

int Version::getMinor() {
    // Extract the second token corresponding to the minor version number.
    const QStringList parts = getString().split('.');
    if (parts.size() > 1) {
        return parts[1].toInt();
    }
    return 0;
}

int Version::getPatch() {
    // Extract the third token corresponding to the patch version number.
    const QStringList parts = getString().split('.');
    if (parts.size() > 2) {
        return parts[2].toInt();
    }
    return 0;
}

} // namespace gridlock::core
