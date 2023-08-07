#pragma once

#ifdef BLACO_USE_CPU_AFFINITY // see config.hpp

#if defined(__linux__)
#include <sched.h>
#elif defined(_WIN32)
#include "Windows.h"
#else
#error "Unsupported OS"
#endif

namespace bl {

namespace detail {

    /*
     * @brief Set the cpu affinity for the caller thread.
     * @param cpu
     */
    inline void set_cpu_affinity(int cpu) {
#if defined(__linux)
        ::cpu_set_t cpu_set;
        CPU_SET(cpu, &cpu_set);
        bool success = (sched_setaffinity(gettid(), sizeof(cpu_set_t), &cpu_set) == 0);
#elif defined(_WIN32)
        BOOL success = SetThreadAffinityMask(GetCurrentThread(), (1<<cpu));
#endif
        if (!success) [[unlikely]] {
            throw std::system_error{ errno, std::system_category(), "set_cpu_affinity" };
        }
    }

} // namespace detail

} // namespace bl

#endif // ifdef BLACO_USE_CPU_AFFINITY
