#pragma once

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

// Independent of the loader's shared log context. Timestamps use the Windows
// local clock (KST on the user's configured machine), rather than claiming UTC.
namespace MinimapDiagnosticDetail
{
    inline std::mutex& Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    inline std::wstring& OverridePath()
    {
        static std::wstring path;
        return path;
    }

    // Caller holds Mutex(). No directory is created and no installation file
    // other than minimap_mod.log is written.
    inline std::wstring LogPath()
    {
        if (!OverridePath().empty()) return OverridePath();
        wchar_t executable[32768] = {};
        const DWORD size = GetModuleFileNameW(nullptr, executable, 32768);
        if (!size || size >= 32768) return {};
        std::wstring path(executable, size);
        const auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return {};
        path.resize(slash + 1);
        path += L"minimap_mod.log";
        return path;
    }
}

inline void WriteDiagnosticLog(const std::string& message) noexcept
{
    // Logging must never throw into a game callback or recursively call itself.
    // This catch covers only logger-owned C++ allocation/locking work, not game
    // or Vulkan operations. It deliberately does not install a broad SEH guard.
    try
    {
        std::lock_guard<std::mutex> lock(MinimapDiagnosticDetail::Mutex());
        const std::wstring path = MinimapDiagnosticDetail::LogPath();
        if (path.empty()) return;
        SYSTEMTIME time{};
        GetLocalTime(&time);
        char prefix[128] = {};
        const int count = std::snprintf(prefix, sizeof(prefix),
            "%04u-%02u-%02u %02u:%02u:%02u.%03u local pid=%lu tid=%lu ",
            static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
            static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
            static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
            static_cast<unsigned>(time.wMilliseconds),
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetCurrentThreadId()));
        if (count <= 0 || static_cast<std::size_t>(count) >= sizeof(prefix)) return;
        std::string line(prefix, static_cast<std::size_t>(count));
        line += message; // Caller supplies ASCII or UTF-8, never an ANSI conversion.
        line += "\r\n";
        if (line.size() > MAXDWORD) return;
        HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        CloseHandle(file); // Close each append so external readers see fresh data.
    }
    catch (...)
    {
        // There is intentionally no secondary logging path or recursion.
    }
}

inline void InitializeDiagnosticLog(const wchar_t* testPath = nullptr) noexcept
{
    // The optional path exists for isolated tests; production uses the game EXE
    // directory. Keep its lifetime internal and serialize it with appends.
    try
    {
        {
            std::lock_guard<std::mutex> lock(MinimapDiagnosticDetail::Mutex());
            MinimapDiagnosticDetail::OverridePath() = testPath ? testPath : L"";
        }
        WriteDiagnosticLog("[Minimap] diagnostic session started");
    }
    catch (...)
    {
    }
}

// POD only: safe to populate inside a narrow SEH leaf without allocation,
// formatting, mutexes or loader callbacks. Log the record outside the filter.
struct DiagnosticFault
{
    DWORD code;
    std::uintptr_t address;
};

inline LONG CaptureDiagnosticFault(EXCEPTION_POINTERS* exception,
    DiagnosticFault* fault) noexcept
{
    if (!exception || !exception->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;
    if (fault)
    {
        fault->code = code;
        fault->address = reinterpret_cast<std::uintptr_t>(
            exception->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
