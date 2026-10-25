#ifndef _CLI_COMMANDS_H_
#define _CLI_COMMANDS_H_

#include <functional>
#include <string>
#include <vector>

#include "Debugger.h"
#include "GdbStub.h"

struct SDL_Renderer;

namespace commands {
    struct Context {
        Debugger& debugger;
        GdbStub& gdbStub;
        SDL_Renderer* renderer;
    };

    void Register();
}  // namespace commands

#endif  // _CLI_COMMANDS_H_
