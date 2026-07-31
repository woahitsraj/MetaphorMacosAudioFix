#include "log.hpp"

namespace {
SRWLOCK g_log_lock = SRWLOCK_INIT;
HANDLE g_log_file = INVALID_HANDLE_VALUE;

void Write(const char* level, const char* format, va_list args)
{
    char message[2048]{};
    std::vsnprintf(message, sizeof(message), format, args);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    char line[2304]{};
    const int length = std::snprintf(
        line,
        sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u tid=%lu level=%s %s\r\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        static_cast<unsigned long>(GetCurrentThreadId()),
        level,
        message
    );
    if (length <= 0) {
        return;
    }

    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_log_file, line, static_cast<DWORD>(std::min<int>(length, sizeof(line) - 1)), &written, nullptr);
        if (level[0] != 'I') {
            FlushFileBuffers(g_log_file);
        }
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}
}

namespace Log {
void Init(const std::filesystem::path& path, bool)
{
    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_file == INVALID_HANDLE_VALUE && !path.empty()) {
        g_log_file = CreateFileW(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

void Info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Write("INFO", format, args);
    va_end(args);
}

void Warn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Write("WARN", format, args);
    va_end(args);
}

void Error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Write("ERROR", format, args);
    va_end(args);
}

void Shutdown()
{
    if (!TryAcquireSRWLockExclusive(&g_log_lock)) {
        return;
    }
    if (g_log_file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_log_file);
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}
}
