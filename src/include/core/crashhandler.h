#ifndef BEDROCKMAP_CRASHHANDLER_H
#define BEDROCKMAP_CRASHHANDLER_H

namespace crashhandler {

    // Install crash reporting for this process. Hardware faults (access violation,
    // divide-by-zero, ...) and abort-style signals are caught and turned into a
    // symbolized stack trace on stderr plus a crash_<ts>.log in the log directory.
    //
    // Windows uses an SEH unhandled-exception filter and libbacktrace (DWARF read
    // straight from the exe, no PDB needed); POSIX installs sigaction handlers and
    // symbolises with backtrace()/dladdr(). Call once, early in main().
    void install();

}  // namespace crashhandler

#endif  // BEDROCKMAP_CRASHHANDLER_H
