#include "winapi.hpp"

#if NHTTP_OS_WINDOWS
#include <Windows.h>

int nhttp::hal::x_win_close_handle(void* handle) {
    return CloseHandle(handle);
}
#endif

