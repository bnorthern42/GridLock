/**
 * @file IDebugCommand.hpp
 * @brief Command pattern interface for dispatching MPI debugger control actions.
 *
 * Encapsulates user-initiated debugging operations (launch, continue, step,
 * pause, terminate, toggle breakpoint) so UI toolbars and keyboard shortcuts
 * remain decoupled from backend coordinator protocol specifics.
 */

#pragma once

namespace gridlock::core::commands {

/**
 * @brief Abstract base interface for executable debugger commands.
 */
class IDebugCommand {
public:
    /** @brief Virtual destructor for polymorphic command ownership. */
    virtual ~IDebugCommand() = default;

    /** @brief Executes the encapsulated debugging operation against the active backend. */
    virtual void execute() = 0;
};

} // namespace gridlock::core::commands
