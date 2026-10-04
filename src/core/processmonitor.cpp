#include "processmonitor.h"

#ifdef _WIN32
// clang-format off
#include <Windows.h>
#include <Psapi.h>
// clang-format on
#else
#include <sys/resource.h>
#include <unistd.h>

#include <cstdio>
#endif

namespace processmonitor {

    double memoryUsageMiB() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
        return static_cast<double>(pmc.WorkingSetSize >> 20);
#else
        // Linux exposes the resident set size in /proc/self/statm (second field,
        // in pages). Reading it is cheaper than a syscall and matches the Windows
        // working-set semantics closely enough for the status bar readout.
        if (FILE* statm = std::fopen("/proc/self/statm", "r")) {
            unsigned long total_pages = 0;
            unsigned long resident_pages = 0;
            const int fields = std::fscanf(statm, "%lu %lu", &total_pages, &resident_pages);
            std::fclose(statm);
            if (fields == 2) {
                return static_cast<double>(resident_pages) * static_cast<double>(::sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
            }
        }
        // Portable fallback: peak RSS in kilobytes (getrusage).
        rusage usage{};
        if (::getrusage(RUSAGE_SELF, &usage) == 0) {
            return static_cast<double>(usage.ru_maxrss) / 1024.0;
        }
        return 0.0;
#endif
    }

}  // namespace processmonitor
