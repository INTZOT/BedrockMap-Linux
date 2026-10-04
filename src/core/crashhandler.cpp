#include "crashhandler.h"

#ifdef _WIN32

#include <backtrace.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include <string>

#include "loguru/loguru.hpp"

namespace crashhandler {
    namespace {

        // Hardware faults (bad pointer deref, divide by zero, ...) do NOT become
        // C signals on Windows — they surface as SEH exceptions. So we install an
        // unhandled-exception filter and unwind the x64 stack by walking the RBP
        // chain captured in the exception CONTEXT, symbolizing each PC with
        // libbacktrace (reads the DWARF straight out of the exe, no PDB needed).
        // libbacktrace state is created once at startup and only queried afterwards.
        struct backtrace_state* g_state = nullptr;
        bool g_installed = false;

        // The report is accumulated in memory and handed to loguru in one go:
        // loguru dispatches it to stderr and every registered file sink. Doing
        // the formatting once keeps lock/alloc churn out of the fault handler.
        struct Sink {
            std::string text;
        };

        void sink_printf(Sink* s, const char* fmt, ...) {
            char buf[2048];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof buf, fmt, args);
            va_end(args);
            s->text += buf;
        }

        void backtrace_error_cb(void* data, const char* msg, int errnum) {
            auto* sink = static_cast<Sink*>(data);
            sink_printf(sink, "  [backtrace] %s (errno %d)\n", msg, errnum);
        }

        int frame_cb(void* data, uintptr_t pc, const char* filename, int lineno, const char* function) {
            auto* sink = static_cast<Sink*>(data);
            if (function) {
                sink_printf(sink, "  %s\n      at %s:%d (0x%llx)\n", function, filename ? filename : "?", lineno,
                            static_cast<unsigned long long>(pc));
            } else if (filename) {
                sink_printf(sink, "  %s:%d (0x%llx)\n", filename, lineno, static_cast<unsigned long long>(pc));
            } else {
                sink_printf(sink, "  0x%llx\n", static_cast<unsigned long long>(pc));
            }
            return 0;  // keep walking
        }

        void symbol_pc(Sink* sink, uintptr_t pc) { backtrace_pcinfo(g_state, pc, frame_cb, backtrace_error_cb, sink); }

        // x64 unwinding uses the per-function .pdata/.xdata unwind info via
        // RtlLookupFunctionEntry/RtlVirtualUnwind (the same metadata the OS uses for
        // SEH), not a hand-rolled RBP chain. MinGW GCC builds many frames with RBP as
        // a plain base register for locals (push regs; lea rbp,[rsp+N]), so walking
        // [RBP] -> caller RBP reads garbage and stops after the first frame, while the
        // unwind codes in .pdata describe the real frame layout for every function.
        void unwind_from_context(Sink* sink, PCONTEXT ctx, int skip_frames) {
            for (int frame = 0; frame < 64 && ctx->Rip != 0; ++frame) {
                if (frame >= skip_frames) symbol_pc(sink, static_cast<uintptr_t>(ctx->Rip));
                DWORD64 image_base = 0;
                auto* entry = RtlLookupFunctionEntry(ctx->Rip, &image_base, nullptr);
                if (entry == nullptr) break;  // leaf frame, no unwind metadata
                PVOID handler_data = nullptr;
                DWORD64 establisher_frame = 0;
                // Handler type 0 = UNW_FLAG_NHANDLER: never run registered handlers.
                RtlVirtualUnwind(0, image_base, ctx->Rip, entry, ctx, &handler_data, &establisher_frame, nullptr);
            }
        }

        const char* exception_name(DWORD code) {
            switch (code) {
                case EXCEPTION_ACCESS_VIOLATION:
                    return "ACCESS_VIOLATION";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:
                    return "INT_DIVIDE_BY_ZERO";
                case EXCEPTION_STACK_OVERFLOW:
                    return "STACK_OVERFLOW";
                case EXCEPTION_ILLEGAL_INSTRUCTION:
                    return "ILLEGAL_INSTRUCTION";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                    return "ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    return "DATATYPE_MISALIGNMENT";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                    return "FLT_DIVIDE_BY_ZERO";
                default:
                    return "UNKNOWN_EXCEPTION";
            }
        }

        LONG WINAPI exception_filter(EXCEPTION_POINTERS* ep) {
            // Prefer the OS default handling so a debugger still breaks; this filter
            // only runs when nothing else handled the fault anyway.
            Sink sink;
            const auto code = ep->ExceptionRecord->ExceptionCode;
            sink_printf(&sink, "\n===== %s (0x%08lx) caught =====\n", exception_name(code), static_cast<unsigned long>(code));
#if defined(_WIN64)
            if (auto* access = ep->ExceptionRecord->ExceptionInformation;
                access && ep->ExceptionRecord->NumberParameters >= 2 && code == EXCEPTION_ACCESS_VIOLATION) {
                const char* kind = access[0] == 0 ? "read" : (access[0] == 1 ? "write" : "exec");
                sink_printf(&sink, "  %s of address 0x%llx\n", kind, static_cast<unsigned long long>(access[1]));
            }
            sink_printf(&sink, "Stack trace:\n");
            if (g_state) {
                // Start from the faulting instruction's register context so the crashing
                // function is printed first.
                CONTEXT ctx = *ep->ContextRecord;
                unwind_from_context(&sink, &ctx, 0);
            } else {
                sink_printf(&sink, "  (backtrace state not initialized)\n");
            }
#else
            sink_printf(&sink, "Stack trace:\n  (x86 unwind not supported)\n");
#endif
            sink_printf(&sink, "===== end =====\n");

            // Route through loguru: the report lands in the normal run log
            // (with the pre-crash context above it) and on stderr.
            LOG_F(ERROR, "%s", sink.text.c_str());

            // Let the system terminate the process with the normal crash semantics.
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // CRT-originated fatal signals (abort/assert) still arrive as C signals.
        void on_abort(int sig) {
            signal(sig, SIG_DFL);

            Sink sink;
            sink_printf(&sink, "\n===== signal %d caught =====\n", sig);
            sink_printf(&sink, "Stack trace:\n");
            if (g_state) {
                // Synchronous call from the aborting code: snapshot the live stack and
                // walk it with the same metadata-driven unwinder as the SEH path.
                // libbacktrace cannot walk a MinGW PE stack on its own.
                CONTEXT ctx;
                RtlCaptureContext(&ctx);
                unwind_from_context(&sink, &ctx, 1);  // skip this handler's own frame
            } else {
                sink_printf(&sink, "  (backtrace state not initialized)\n");
            }
            sink_printf(&sink, "===== end =====\n");

            // Same routing as the SEH handler: into the normal run log + stderr.
            LOG_F(ERROR, "%s", sink.text.c_str());

            raise(sig);
        }

    }  // namespace

    void install() {
        if (g_installed) return;
        g_installed = true;

        char exe[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            strcpy_s(exe, "BedrockMap");
        }
        // Single-threaded access pattern (created here, read from crash handler).
        g_state = backtrace_create_state(exe, /*threaded=*/0, backtrace_error_cb, nullptr);

        // libbacktrace reads + parses the exe's DWARF lazily on the first lookup.
        // Trigger it now (normal context): the crash handler must not attempt a
        // multi-hundred-MB file read from inside the fault handler, where a
        // transient lock or slow read can make symbolization fail.
        if (g_state) {
            backtrace_pcinfo(
                g_state, reinterpret_cast<uintptr_t>(&install), [](void*, uintptr_t, const char*, int, const char*) { return 0; },
                [](void*, const char*, int) {}, nullptr);
        }

        // Catches hardware faults (access violation, divide by zero, ...).
        SetUnhandledExceptionFilter(exception_filter);
        // Catches CRT-originated aborts (assert failures, loguru FATAL, ...).
        signal(SIGABRT, on_abort);
        signal(SIGFPE, on_abort);
    }

}  // namespace crashhandler

#else  // POSIX (Linux, BSD, macOS)

#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include "apppaths.h"
#include "loguru/loguru.hpp"

namespace crashhandler {
    namespace {

        // On POSIX the same fault kinds that Windows reports as SEH exceptions
        // arrive as synchronous signals (SIGSEGV/SIGBUS/SIGFPE/SIGILL), so a single
        // sigaction handler covers hardware faults and abort()/assert failures.
        //
        // The handler stays close to async-signal-safe: the report is written with
        // write(2) to stderr and to <log dir>/crash_<ts>.log, using only fixed-size
        // stack buffers. backtrace()/backtrace_symbols_fd() are glibc extensions
        // that may allocate internally; combined with the frame-pointer builds of
        // this project they are the standard way to symbolise without a debugger.
        bool g_installed = false;
        char g_crash_file_prefix[PATH_MAX];

        void writeAll(int fd, const char* text, size_t length) {
            while (length > 0) {
                const ssize_t written = ::write(fd, text, length);
                if (written <= 0) return;
                text += written;
                length -= static_cast<size_t>(written);
            }
        }

        void appendFrameSymbol(char* out, size_t size, void* address) {
            Dl_info info{};
            if (::dladdr(address, &info) != 0 && info.dli_sname != nullptr) {
                // Report the module-relative offset so the frame can still be
                // resolved when the binary is stripped of local symbols.
                const auto offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(info.dli_saddr);
                ::snprintf(out, size, "%s + 0x%zx", info.dli_sname, static_cast<size_t>(offset));
            } else {
                ::snprintf(out, size, "0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)));
            }
        }

        void handler(int sig, siginfo_t* info, void* /*ucontext*/) {
            // Restore the default disposition immediately: a fault inside the
            // reporter must kill the process instead of recursing forever.
            ::signal(sig, SIG_DFL);

            char header[256];
            const int header_len =
                ::snprintf(header, sizeof header, "\n===== signal %d (%s) caught =====\nStack trace:\n", sig, ::strsignal(sig));

            void* frames[64];
            const int frame_count = ::backtrace(frames, 64);

            int fd = -1;
            if (g_crash_file_prefix[0] != '\0') {
                char path[PATH_MAX + 32];
                const time_t now = ::time(nullptr);
                ::snprintf(path, sizeof path, "%s%ld.log", g_crash_file_prefix, static_cast<long>(now));
                fd = ::open(path, O_CREAT | O_WRONLY | O_APPEND, 0644);
            }

            const int targets[2] = {STDERR_FILENO, fd};
            char line[512];
            for (const int target : targets) {
                if (target < 0) continue;
                writeAll(target, header, static_cast<size_t>(header_len));
                if (info != nullptr && (sig == SIGSEGV || sig == SIGBUS) && info->si_addr != nullptr) {
                    const int len = ::snprintf(line, sizeof line, "  fault address: %p\n", info->si_addr);
                    writeAll(target, line, static_cast<size_t>(len));
                }
                // glibc prints "path(symbol+offset) [address]" per frame. Build the
                // lines explicitly so the report is readable without addr2line.
                for (int i = 0; i < frame_count; ++i) {
                    char symbol[256];
                    appendFrameSymbol(symbol, sizeof symbol, frames[i]);
                    const int len = ::snprintf(line, sizeof line, "  #%-2d %s\n", i, symbol);
                    writeAll(target, line, static_cast<size_t>(len));
                }
                writeAll(target, "===== end =====\n", ::strlen("===== end =====\n"));
            }
            if (fd >= 0) ::close(fd);

            // Best effort: also leave the tail in the regular run log, where the
            // last lines before the crash give the context.
            LOG_F(ERROR, "signal %d (%s) caught; full report written to stderr and the crash log", sig, ::strsignal(sig));

            // Re-raise with the default handler so the exit status and core-dump
            // behaviour match an unhandled crash (gdb/systemd-coredump still work).
            ::raise(sig);
        }

        void installHandler(int sig) {
            struct sigaction action {};
            action.sa_sigaction = handler;
            action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
            sigemptyset(&action.sa_mask);
            sigaction(sig, &action, nullptr);
        }

    }  // namespace

    void install() {
        if (g_installed) return;
        g_installed = true;

        // The reporter needs a directory that is writable at crash time; resolve it
        // now (the lookup touches the filesystem and is not signal-safe).
        const std::string log_dir = apppaths::logDir().toStdString();
        ::snprintf(g_crash_file_prefix, sizeof g_crash_file_prefix, "%s/crash_", log_dir.c_str());

        // Run the handler on an alternate stack so a stack overflow (SIGSEGV with
        // no usable stack left) can still be reported. SIGSTKSZ is not a compile
        // time constant on recent glibc, so use a fixed, comfortably large stack.
        static char alternate_stack[64 * 1024];
        stack_t stack{};
        stack.ss_sp = alternate_stack;
        stack.ss_size = sizeof alternate_stack;
        sigaltstack(&stack, nullptr);

        installHandler(SIGSEGV);
        installHandler(SIGBUS);
        installHandler(SIGFPE);
        installHandler(SIGILL);
        installHandler(SIGABRT);
    }

}  // namespace crashhandler

#endif  // _WIN32
