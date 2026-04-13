#include "log.hpp"

namespace {
SRWLOCK g_log_lock = SRWLOCK_INIT;
FILE* g_log_file = nullptr;
bool g_verbose = true;

void WriteLine(const char* level, const char* fmt, va_list args)
{
    AcquireSRWLockExclusive(&g_log_lock);
    if (!g_log_file) {
        ReleaseSRWLockExclusive(&g_log_lock);
        return;
    }

    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    std::fprintf(
        g_log_file,
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] %s\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        level,
        buffer
    );
    std::fflush(g_log_file);
    ReleaseSRWLockExclusive(&g_log_lock);
}
}

namespace Log {
void Init(const std::filesystem::path& path, bool verbose)
{
    AcquireSRWLockExclusive(&g_log_lock);
    g_verbose = verbose;
    if (g_log_file) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }

    g_log_file = _wfopen(path.c_str(), L"wb");
    ReleaseSRWLockExclusive(&g_log_lock);
}

void Info(const char* fmt, ...)
{
    if (!g_verbose) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    WriteLine("INFO", fmt, args);
    va_end(args);
}

void Warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteLine("WARN", fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteLine("ERROR", fmt, args);
    va_end(args);
}

void Shutdown()
{
    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_file) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}
}
