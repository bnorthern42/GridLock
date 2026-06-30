#include "Version.hpp"
#include "VersionConfig.hpp"
#include <QStringList>

namespace gridlock::core {

QString Version::getString() {
    return QStringLiteral(GRIDLOCK_VERSION_STRING);
}

int Version::getMajor() {
    QStringList parts = getString().split('.');
    if (parts.size() > 0) return parts[0].toInt();
    return 0;
}

int Version::getMinor() {
    QStringList parts = getString().split('.');
    if (parts.size() > 1) return parts[1].toInt();
    return 0;
}

int Version::getPatch() {
    QStringList parts = getString().split('.');
    if (parts.size() > 2) return parts[2].toInt();
    return 0;
}

} // namespace gridlock::core
