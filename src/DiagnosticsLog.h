#pragma once

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace dxteaching
{

inline const std::string &LogFilePath()
{
    static std::string path = []() {
        char modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        std::string p(modulePath, modulePath + length);
        const size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos)
        {
            p.erase(slash + 1);
        }
        else
        {
            p.clear();
        }
        p += "dx_runtime.log";
        return p;
    }();
    return path;
}

inline void ResetLogFile()
{
    DeleteFileA(LogFilePath().c_str());
}

inline std::string FormatHResult(HRESULT hr)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned int>(hr));
    return std::string(buffer);
}

inline void LogLine(const char *tag, const char *fmt, ...)
{
    if (tag == nullptr)
    {
        tag = "LOG";
    }

    char msgBuffer[1024] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msgBuffer, sizeof(msgBuffer), fmt, args);
    va_end(args);

    SYSTEMTIME st{};
    GetLocalTime(&st);

    char prefix[128] = {};
    std::snprintf(prefix,
                  sizeof(prefix),
                  "[%02u:%02u:%02u.%03u][T%lu][%s] ",
                  static_cast<unsigned>(st.wHour),
                  static_cast<unsigned>(st.wMinute),
                  static_cast<unsigned>(st.wSecond),
                  static_cast<unsigned>(st.wMilliseconds),
                  static_cast<unsigned long>(GetCurrentThreadId()),
                  tag);

    std::string line = std::string(prefix) + msgBuffer + "\r\n";
    OutputDebugStringA(line.c_str());

    static std::mutex sMutex;
    std::lock_guard<std::mutex> lock(sMutex);

    FILE *file = nullptr;
    if (fopen_s(&file, LogFilePath().c_str(), "ab") == 0 && file != nullptr)
    {
        fwrite(line.data(), 1, line.size(), file);
        fflush(file);
        fclose(file);
    }
}

} // namespace dxteaching
