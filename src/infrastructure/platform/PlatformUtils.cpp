#include "crawler/infrastructure/platform/PlatformUtils.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace orchestrator::modules::crawler::infrastructure::platform {

void setupConsole()
{
#ifdef _WIN32
    // Get the standard input handle.
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    // Get the current console mode.
    if (GetConsoleMode(hInput, &mode)) {
        // Remove Quick Edit Mode to prevent the console from pausing on click.
        mode &= ~ENABLE_QUICK_EDIT_MODE;
        // Set the new mode.
        SetConsoleMode(hInput, mode);
    }
#endif
    // Other platforms can have their setup here.
}

} // namespace orchestrator::modules::crawler::infrastructure::platform
