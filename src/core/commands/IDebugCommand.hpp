#pragma once

namespace gridlock::core::commands {

class IDebugCommand {
public:
    virtual ~IDebugCommand() = default;
    virtual void execute() = 0;
};

} // namespace gridlock::core::commands
