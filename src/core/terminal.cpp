#include "terminal.h"

#ifdef _WIN32
#include "terminal_win.h"
#else
#include "terminal_posix.h"
#endif

namespace nightforge {

Terminal* create_terminal() {
#ifdef _WIN32
    return new TerminalWin();
#else
    return new TerminalPosix();
#endif
}

} // namespace nightforge