#pragma once
#include <QString>

namespace gridlock::core {

class Version {
public:
    static QString getString();
    static int getMajor();
    static int getMinor();
    static int getPatch();
};

} // namespace gridlock::core
