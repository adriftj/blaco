#pragma once

#include "blconfig.h"

#if defined(_WIN32)
	#include "os/windows/blconfig_windows.hpp"
#else
    #include "os/linux/blconfig_linux.hpp"
#endif

#include <coroutine>

namespace bl {
	namespace config {
    } // namespace config
}
