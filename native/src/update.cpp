#include "awfan/update.hpp"

#include <windows.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace awfan {
namespace {

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

std::runtime_error win_error(
    const std::string& operation,
    const DWORD code = GetLastError()
) {
    return std::runtime_error(
        operation + " failed with Windows error " + std::to_string(code) + "."
    );
}

std::wstring module_directory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );

    if (length == 0 || length >= buffer.size()) {
        throw win_error("GetModuleFileNameW");
    }

    std::wstring path(buffer.data(), length);
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        throw std::runtime_error("Could not determine the awfan directory.");
    }

    return path.substr(0, separator);
}

std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

}  // namespace

int run_update(const bool check_only, const bool force) {
    const std::wstring install_dir = module_directory();
    const std::wstring script = install_dir + L"\\update.ps1";

    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
        throw std::runtime_error(
            "update.ps1 is missing. Reinstall awfan from the latest package."
        );
    }

    std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File "
        + quote(script)
        + L" -InstallDir " + quote(install_dir);

    if (check_only) {
        command += L" -CheckOnly";
    } else {
        command += L" -ParentPid " + std::to_wstring(GetCurrentProcessId());
    }

    if (force) {
        command += L" -Force";
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            install_dir.c_str(),
            &startup,
            &process
        )) {
        throw win_error("Starting the updater");
    }

    Handle process_handle(process.hProcess);
    Handle thread_handle(process.hThread);

    if (!check_only) {
        return 0;
    }

    WaitForSingleObject(process_handle.get(), INFINITE);

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
        throw win_error("Reading the updater result");
    }

    return static_cast<int>(exit_code);
}

}  // namespace awfan
