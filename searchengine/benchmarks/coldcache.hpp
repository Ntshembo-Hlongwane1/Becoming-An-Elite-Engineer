// benchmarks/coldcache.hpp
//
// Evicts the Windows file cache so that a cold benchmark genuinely reads from
// the disk, the way a first-ever startup on a fresh machine would.
//
// Without this, only the first run after a reboot is a real cold run. Windows
// keeps recently-read files in spare RAM, so every later run is served out of
// memory and reports a fraction of the true cost - the same effect that makes
// an 18 minute build look like a few seconds on the second attempt.
//
// This is the same mechanism Sysinternals RAMMap (-Ew) and EmptyStandbyList
// use: NtSetSystemInformation with the memory-list classes. Those classes are
// undocumented but have been stable since Windows 7. It requires elevation,
// because emptying the standby list affects the whole machine, not just this
// process.
#pragma once

#include <string>

#ifdef _WIN32

#include <windows.h>

namespace bench {
namespace detail {

// SYSTEM_INFORMATION_CLASS value for the memory list commands.
constexpr int kSystemMemoryListInformation = 0x50;

// SYSTEM_MEMORY_LIST_COMMAND values, applied in this order for a full evict:
// push private working sets out to the standby list, flush dirty pages to
// disk, then discard the standby list (which is where cached file data lives).
constexpr int kMemoryEmptyWorkingSets = 2;
constexpr int kMemoryFlushModifiedList = 3;
constexpr int kMemoryPurgeStandbyList = 4;

using NtSetSystemInformationFn = LONG(WINAPI*)(int, PVOID, ULONG);

// Emptying the standby list is privileged. Ask for the privilege the same way
// RAMMap does; this only succeeds in an elevated process.
inline bool EnablePrivilege(const wchar_t* name) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    bool ok = LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid) != 0;
    if (ok) {
        ok = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges),
                                   nullptr, nullptr) != 0;
        // AdjustTokenPrivileges reports success even when it silently dropped
        // the privilege, so the real verdict is in GetLastError().
        ok = ok && GetLastError() == ERROR_SUCCESS;
    }

    CloseHandle(token);
    return ok;
}

inline bool SendMemoryCommand(NtSetSystemInformationFn fn, int command) {
    int value = command;
    return fn(kSystemMemoryListInformation, &value, sizeof(value)) >= 0;
}

}  // namespace detail

// Drops the OS file cache. Returns false (with a reason in *error) if the
// process lacks the rights to do so - never silently leaves the cache warm,
// because a warm run reported as a cold one is worse than no number at all.
inline bool PurgeFileSystemCache(std::string* error) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        if (error) *error = "could not load ntdll.dll";
        return false;
    }

    auto fn = reinterpret_cast<detail::NtSetSystemInformationFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSetSystemInformation")));
    if (!fn) {
        if (error) *error = "NtSetSystemInformation not available";
        return false;
    }

    // Spelled out rather than using SE_PROF_SINGLE_PROCESS_NAME, which resolves
    // to the ANSI literal unless the whole build defines UNICODE.
    if (!detail::EnablePrivilege(L"SeProfileSingleProcessPrivilege")) {
        if (error) *error = "SeProfileSingleProcessPrivilege denied; not elevated";
        return false;
    }

    // Working sets and the modified list are best-effort: they reduce how much
    // survives the purge, but the standby purge is the one that must succeed.
    detail::SendMemoryCommand(fn, detail::kMemoryEmptyWorkingSets);
    detail::SendMemoryCommand(fn, detail::kMemoryFlushModifiedList);

    if (!detail::SendMemoryCommand(fn, detail::kMemoryPurgeStandbyList)) {
        if (error) *error = "purging the standby list was refused; not elevated";
        return false;
    }

    return true;
}

}  // namespace bench

#else

namespace bench {
inline bool PurgeFileSystemCache(std::string* error) {
    if (error) *error = "cache eviction is only implemented for Windows";
    return false;
}
}  // namespace bench

#endif  // _WIN32
