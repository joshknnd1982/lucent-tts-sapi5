#pragma once
//
// Shared file logger for the Lucent SAPI 5 wrapper.  Every component (the x86 and x64
// engine DLLs, the configuration utility, the test tools) writes to
//   %LOCALAPPDATA%\LucentSAPI\logs\<exe>_<arch>_<pid>.log
// The engine child's stderr goes to engine_<pid>.log in the same folder.
//
// The file is opened with a plain "a" mode and _SH_DENYNO so it can be read while the
// process runs; never use ccs= modes with narrow printf on the static CRT.
//
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <share.h>

namespace lucent {

class Logger {
public:
    static Logger& instance() {
        static Logger g;
        return g;
    }

    // Enable or disable logging.  Off by default until configure() is called.
    void configure(bool enabled, const wchar_t* component) {
        EnterCriticalSection(&cs_);
        enabled_ = enabled;
        if (enabled_ && !file_) {
            open(component);
        }
        LeaveCriticalSection(&cs_);
    }

    bool enabled() const { return enabled_; }

    std::wstring logDirectory() const { return dir_; }

    void log(const char* fmt, ...) {
        if (!enabled_) return;
        va_list ap;
        va_start(ap, fmt);
        vlog(fmt, ap);
        va_end(ap);
    }

    void vlog(const char* fmt, va_list ap) {
        if (!enabled_) return;
        EnterCriticalSection(&cs_);
        if (file_) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(file_, "%02d:%02d:%02d.%03d [%5lu] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
            vfprintf(file_, fmt, ap);
            fputc('\n', file_);
            fflush(file_);
        }
        LeaveCriticalSection(&cs_);
    }

    static std::wstring defaultLogDirectory() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L"C:\\";
        dir += L"\\LucentSAPI";
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

private:
    Logger() { InitializeCriticalSection(&cs_); }
    ~Logger() {
        if (file_) fclose(file_);
        DeleteCriticalSection(&cs_);
    }

    void open(const wchar_t* component) {
        dir_ = defaultLogDirectory();
        wchar_t exe[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* base = wcsrchr(exe, L'\\');
        base = base ? base + 1 : exe;
        std::wstring name(base);
        size_t dot = name.rfind(L'.');
        if (dot != std::wstring::npos) name.resize(dot);
#ifdef _WIN64
        const wchar_t* arch = L"x64";
#else
        const wchar_t* arch = L"x86";
#endif
        wchar_t path[MAX_PATH * 2];
        swprintf_s(path, L"%s\\%s_%s_%s_%lu.log", dir_.c_str(), name.c_str(), component, arch, GetCurrentProcessId());
        file_ = _wfsopen(path, L"a", _SH_DENYNO);
        if (file_) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(file_, "==== %04d-%02d-%02d %02d:%02d:%02d %ls (%ls) pid %lu ====\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, exe, arch, GetCurrentProcessId());
            fflush(file_);
        }
    }

    CRITICAL_SECTION cs_;
    FILE* file_ = nullptr;
    bool enabled_ = false;
    std::wstring dir_;
};

}  // namespace lucent

#define LLOG(...) ::lucent::Logger::instance().log(__VA_ARGS__)
