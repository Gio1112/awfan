#include "awfan/broker.hpp"
#include "awfan/platform_features.hpp"
#include "awfan/update.hpp"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
constexpr wchar_t kVersion[] = L"1.2.0";

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~Handle() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() noexcept {
        const HANDLE value = value_;
        value_ = INVALID_HANDLE_VALUE;
        return value;
    }
    void reset(HANDLE value = INVALID_HANDLE_VALUE) noexcept {
        if (valid()) {
            CloseHandle(value_);
        }
        value_ = value;
    }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

struct ProfileSnapshot {
    std::optional<int> current_raw;
    std::string control_mode{"unknown"};
    std::vector<int> profiles;
};

struct SafetyOptions {
    std::vector<std::wstring> core_arguments;
    std::optional<std::int64_t> duration_ms;
    bool until_reboot{false};
};

struct BrokerInfo {
    bool reachable{false};
    std::optional<DWORD> process_id;
    std::optional<std::int64_t> uptime_seconds;
    bool task_registered{false};
    std::string frontend_version;
    std::string core_version;
    std::string broker_version;
};

std::runtime_error windows_error(
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
    if (length == 0 || length >= static_cast<DWORD>(buffer.size())) {
        throw windows_error("GetModuleFileNameW");
    }
    std::wstring path(buffer.data(), length);
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        throw std::runtime_error("Could not determine the awfan installation directory.");
    }
    return path.substr(0, separator);
}

std::wstring quote_argument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::string trim(std::string value) {
    const auto not_space = [](const unsigned char character) {
        return !std::isspace(character);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool has_argument(
    const std::vector<std::wstring>& arguments,
    const std::wstring& option
) {
    return std::find(arguments.begin(), arguments.end(), option) != arguments.end();
}

awfan::BrokerResponse run_process_capture(
    const std::wstring& executable,
    const std::vector<std::wstring>& arguments,
    const std::wstring& working_directory = L""
) {
    SECURITY_ATTRIBUTES inherited{};
    inherited.nLength = sizeof(inherited);
    inherited.bInheritHandle = TRUE;

    HANDLE raw_read = nullptr;
    HANDLE raw_write = nullptr;
    if (!CreatePipe(&raw_read, &raw_write, &inherited, 0)) {
        throw windows_error("CreatePipe");
    }
    Handle output_read(raw_read);
    Handle output_write(raw_write);
    if (!SetHandleInformation(output_read.get(), HANDLE_FLAG_INHERIT, 0)) {
        throw windows_error("SetHandleInformation");
    }

    Handle null_input(CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inherited,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));
    if (!null_input.valid()) {
        throw windows_error("Opening NUL for process input");
    }

    std::wstring command_line = quote_argument(executable);
    for (const auto& argument : arguments) {
        command_line += L" ";
        command_line += quote_argument(argument);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_input.get();
    startup.hStdOutput = output_write.get();
    startup.hStdError = output_write.get();

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            working_directory.empty() ? nullptr : working_directory.c_str(),
            &startup,
            &process
        )) {
        throw windows_error("Starting a child process");
    }

    Handle process_handle(process.hProcess);
    Handle thread_handle(process.hThread);
    output_write.reset();

    awfan::BrokerResponse result;
    std::array<char, 8192> buffer{};
    while (true) {
        DWORD read = 0;
        if (!ReadFile(
                output_read.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read,
                nullptr
            )) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                break;
            }
            throw windows_error("Reading child-process output");
        }
        if (read == 0) {
            break;
        }
        result.output.append(buffer.data(), read);
    }

    WaitForSingleObject(process_handle.get(), INFINITE);
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
        throw windows_error("Reading child-process result");
    }
    result.exit_code = static_cast<int>(exit_code);
    return result;
}

awfan::BrokerResponse run_core_capture(
    const std::vector<std::wstring>& arguments
) {
    const std::wstring directory = module_directory();
    const std::wstring executable = directory + L"\\awfan-core.exe";
    if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
        throw std::runtime_error(
            "awfan-core.exe is missing. Repair the awfan installation."
        );
    }
    return run_process_capture(executable, arguments, directory);
}

std::vector<wchar_t*> argument_pointers(std::vector<std::wstring>& arguments) {
    std::vector<wchar_t*> pointers;
    pointers.reserve(arguments.size());
    for (auto& argument : arguments) {
        pointers.push_back(argument.data());
    }
    return pointers;
}

awfan::BrokerResponse execute_underlying(
    const std::vector<std::wstring>& arguments,
    const bool decorate = true
) {
    if (arguments.empty()) {
        throw std::runtime_error("Internal command was empty.");
    }

    awfan::BrokerResponse result;
    if (awfan::command_requires_broker(arguments.front())
        && !awfan::is_process_elevated()) {
        std::vector<std::wstring> request_arguments;
        request_arguments.reserve(arguments.size() + 1);
        request_arguments.push_back(module_directory() + L"\\awfan.exe");
        request_arguments.insert(
            request_arguments.end(),
            arguments.begin(),
            arguments.end()
        );
        auto pointers = argument_pointers(request_arguments);
        const auto response = awfan::request_broker(
            static_cast<int>(pointers.size()),
            pointers.data()
        );
        if (!response.has_value()) {
            throw std::runtime_error(
                "The elevated background broker is unavailable. Run "
                "'awfan broker repair' and try again."
            );
        }
        result = *response;
    } else {
        result = run_core_capture(arguments);
    }

    if (decorate) {
        result.output = awfan::decorate_profile_output(
            arguments.front(),
            result.output
        );
    }
    return result;
}

ProfileSnapshot parse_profile_snapshot(const std::string& json) {
    ProfileSnapshot snapshot;
    std::smatch match;
    const std::regex current(R"("current"\s*:\s*(null|-?[0-9]+))");
    if (std::regex_search(json, match, current) && match[1].str() != "null") {
        snapshot.current_raw = std::stoi(match[1].str());
    }
    const std::regex mode(R"AW("rememberedControlMode"\s*:\s*"([^"]+)")AW");
    if (std::regex_search(json, match, mode)) {
        snapshot.control_mode = match[1].str();
    }
    const std::regex profiles(R"("profiles"\s*:\s*\[([^\]]*)\])");
    if (std::regex_search(json, match, profiles)) {
        const std::string list = match[1].str();
        const std::regex number(R"(-?[0-9]+)");
        for (std::sregex_iterator iterator(list.begin(), list.end(), number), end;
             iterator != end;
             ++iterator) {
            snapshot.profiles.push_back(std::stoi((*iterator).str()));
        }
    }
    return snapshot;
}

ProfileSnapshot query_profiles() {
    const auto result = execute_underlying({L"profiles", L"--json"}, false);
    if (result.exit_code != 0) {
        throw std::runtime_error(
            "Could not read the current firmware profile: " + trim(result.output)
        );
    }
    return parse_profile_snapshot(result.output);
}

std::optional<awfan::RestoreTarget> remember_current_firmware_profile() {
    const ProfileSnapshot snapshot = query_profiles();
    if (!snapshot.current_raw.has_value() || *snapshot.current_raw == 0) {
        return awfan::load_restore_target();
    }
    const auto found = std::find(
        snapshot.profiles.begin(),
        snapshot.profiles.end(),
        *snapshot.current_raw
    );
    if (found == snapshot.profiles.end()) {
        return awfan::load_restore_target();
    }
    awfan::RestoreTarget target;
    target.profile_index = static_cast<int>(
        std::distance(snapshot.profiles.begin(), found)
    );
    target.raw_profile = *snapshot.current_raw;
    if (target.profile_index > 0) {
        awfan::save_restore_target(target);
        return target;
    }
    return awfan::load_restore_target();
}

void remember_selected_profile(const int profile_index) {
    if (profile_index <= 0) {
        return;
    }
    try {
        const ProfileSnapshot snapshot = query_profiles();
        if (snapshot.current_raw.has_value() && *snapshot.current_raw != 0) {
            awfan::save_restore_target({profile_index, *snapshot.current_raw});
            return;
        }
    } catch (...) {
    }
    awfan::save_restore_target({profile_index, 0x9f + profile_index});
}

SafetyOptions parse_safety_options(
    const std::vector<std::wstring>& arguments
) {
    SafetyOptions options;
    if (arguments.empty()) {
        return options;
    }
    options.core_arguments.push_back(arguments.front());
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::wstring& argument = arguments[index];
        if (argument == L"--for") {
            if (index + 1 >= arguments.size()) {
                throw std::runtime_error("Missing duration after --for.");
            }
            if (options.until_reboot || options.duration_ms.has_value()) {
                throw std::runtime_error(
                    "Use either --for or --until-reboot, not both."
                );
            }
            options.duration_ms = awfan::parse_duration_milliseconds(
                arguments[++index]
            );
        } else if (argument == L"--until-reboot") {
            if (options.until_reboot || options.duration_ms.has_value()) {
                throw std::runtime_error(
                    "Use either --for or --until-reboot, not both."
                );
            }
            options.until_reboot = true;
        } else {
            options.core_arguments.push_back(argument);
        }
    }
    return options;
}

void add_timer_result(
    awfan::BrokerResponse& result,
    const SafetyOptions& options,
    const awfan::RestoreTarget& target
) {
    if (!options.duration_ms.has_value() && !options.until_reboot) {
        return;
    }
    const bool json = has_argument(options.core_arguments, L"--json");
    std::string description;
    if (options.duration_ms.has_value()) {
        description = "Firmware control will be restored to "
            + awfan::profile_name_from_index(target.profile_index)
            + " after " + awfan::format_duration(*options.duration_ms) + ".";
    } else {
        description = "Firmware control will be restored to "
            + awfan::profile_name_from_index(target.profile_index)
            + " when the broker next starts.";
    }
    if (!json) {
        if (!result.output.empty() && result.output.back() != '\n') {
            result.output.push_back('\n');
        }
        result.output += "Safety timer: " + description + "\n";
        return;
    }
    const std::size_t closing = result.output.rfind('}');
    if (closing != std::string::npos) {
        const std::string object =
            ", \"safetyRestore\": {\"profileIndex\": "
            + std::to_string(target.profile_index)
            + ", \"profileName\": \""
            + awfan::json_escape_text(
                awfan::profile_name_from_index(target.profile_index)
            )
            + "\", \"durationMs\": "
            + (options.duration_ms.has_value()
                ? std::to_string(*options.duration_ms)
                : "null")
            + ", \"onBrokerStart\": "
            + (options.until_reboot ? "true" : "false") + "}";
        result.output.insert(closing, object);
    }
}

awfan::BrokerResponse execute_manual_command(
    const std::vector<std::wstring>& arguments
) {
    const SafetyOptions options = parse_safety_options(arguments);
    const auto restore_target = remember_current_firmware_profile();
    if ((options.duration_ms.has_value() || options.until_reboot)
        && !restore_target.has_value()) {
        throw std::runtime_error(
            "A timed manual command needs a known firmware profile to restore. "
            "Select Balanced, Cool, Quiet, or Performance first."
        );
    }

    awfan::BrokerResponse result = execute_underlying(options.core_arguments);
    if (result.exit_code != 0) {
        return result;
    }

    awfan::clear_timer_state();
    if (restore_target.has_value()) {
        if (options.duration_ms.has_value()) {
            awfan::save_timer_state({
                restore_target->profile_index,
                awfan::current_epoch_milliseconds() + *options.duration_ms,
                false
            });
        } else if (options.until_reboot) {
            awfan::save_timer_state({
                restore_target->profile_index,
                awfan::current_epoch_milliseconds()
                    - static_cast<std::int64_t>(GetTickCount64()),
                true
            });
        }
        add_timer_result(result, options, *restore_target);
    }
    return result;
}

awfan::BrokerResponse execute_profile_command(
    const int profile_index,
    const std::vector<std::wstring>& options
) {
    std::vector<std::wstring> arguments{
        L"auto",
        std::to_wstring(profile_index)
    };
    arguments.insert(arguments.end(), options.begin(), options.end());
    awfan::BrokerResponse result = execute_underlying(arguments);
    if (result.exit_code == 0) {
        awfan::clear_timer_state();
        remember_selected_profile(profile_index);
    }
    return result;
}

std::optional<int> parse_integer(const std::wstring& value) {
    try {
        std::size_t consumed = 0;
        const int result = std::stoi(value, &consumed, 10);
        if (consumed == value.size()) {
            return result;
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::string hex_profile(const int raw) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase
           << std::setw(2) << std::setfill('0') << (raw & 0xff);
    return output.str();
}

std::optional<DWORD> find_broker_process_id() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        return std::nullopt;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return std::nullopt;
    }
    do {
        if (_wcsicmp(entry.szExeFile, L"awfan-broker.exe") == 0) {
            return entry.th32ProcessID;
        }
    } while (Process32NextW(snapshot.get(), &entry));
    return std::nullopt;
}

std::optional<std::int64_t> process_uptime_seconds(const DWORD process_id) {
    Handle process(OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        process_id
    ));
    if (!process.valid()) {
        return std::nullopt;
    }
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process.get(), &creation, &exit, &kernel, &user)) {
        return std::nullopt;
    }
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER created_value{};
    ULARGE_INTEGER now_value{};
    created_value.LowPart = creation.dwLowDateTime;
    created_value.HighPart = creation.dwHighDateTime;
    now_value.LowPart = now.dwLowDateTime;
    now_value.HighPart = now.dwHighDateTime;
    if (now_value.QuadPart < created_value.QuadPart) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(
        (now_value.QuadPart - created_value.QuadPart) / 10000000ULL
    );
}

std::string format_uptime(const std::int64_t seconds) {
    const std::int64_t days = seconds / 86400;
    const std::int64_t hours = (seconds % 86400) / 3600;
    const std::int64_t minutes = (seconds % 3600) / 60;
    std::ostringstream output;
    if (days > 0) {
        output << days << "d ";
    }
    if (hours > 0 || days > 0) {
        output << hours << "h ";
    }
    output << minutes << "m";
    return output.str();
}

BrokerInfo collect_broker_info() {
    BrokerInfo info;
    info.reachable = awfan::broker_is_available();
    info.frontend_version = "awfan 1.2.0";
    info.process_id = find_broker_process_id();
    if (info.process_id.has_value()) {
        info.uptime_seconds = process_uptime_seconds(*info.process_id);
    }
    const std::wstring directory = module_directory();
    try {
        info.core_version = trim(run_process_capture(
            directory + L"\\awfan-core.exe",
            {L"version"},
            directory
        ).output);
    } catch (...) {
        info.core_version = "unavailable";
    }
    try {
        info.broker_version = trim(run_process_capture(
            directory + L"\\awfan-broker.exe",
            {L"--version"},
            directory
        ).output);
    } catch (...) {
        info.broker_version = "unavailable";
    }
    try {
        const auto task = run_process_capture(
            L"C:\\Windows\\System32\\schtasks.exe",
            {L"/Query", L"/FO", L"CSV", L"/NH"}
        );
        info.task_registered = task.exit_code == 0
            && task.output.find("awfan Broker") != std::string::npos;
    } catch (...) {
        info.task_registered = false;
    }
    return info;
}

void print_broker_status(const bool json) {
    const BrokerInfo info = collect_broker_info();
    if (json) {
        std::cout
            << "{\"reachable\": " << (info.reachable ? "true" : "false")
            << ", \"pid\": ";
        if (info.process_id.has_value()) {
            std::cout << *info.process_id;
        } else {
            std::cout << "null";
        }
        std::cout << ", \"uptimeSeconds\": ";
        if (info.uptime_seconds.has_value()) {
            std::cout << *info.uptime_seconds;
        } else {
            std::cout << "null";
        }
        std::cout
            << ", \"taskRegistered\": "
            << (info.task_registered ? "true" : "false")
            << ", \"frontendVersion\": \""
            << awfan::json_escape_text(info.frontend_version)
            << "\", \"coreVersion\": \""
            << awfan::json_escape_text(info.core_version)
            << "\", \"brokerVersion\": \""
            << awfan::json_escape_text(info.broker_version)
            << "\"}\n";
        return;
    }
    std::cout
        << "Broker: " << (info.reachable ? "running" : "unavailable")
        << "\nPID: ";
    if (info.process_id.has_value()) {
        std::cout << *info.process_id;
    } else {
        std::cout << "unavailable";
    }
    std::cout << "\nUptime: ";
    if (info.uptime_seconds.has_value()) {
        std::cout << format_uptime(*info.uptime_seconds);
    } else {
        std::cout << "unavailable";
    }
    std::cout
        << "\nFrontend version: " << info.frontend_version
        << "\nCore version: " << info.core_version
        << "\nBroker version: " << info.broker_version
        << "\nTask: " << (info.task_registered ? "registered" : "not found")
        << "\nPipe: " << (info.reachable ? "reachable" : "unreachable")
        << "\n";
}

int run_broker_restart() {
    std::vector<std::wstring> arguments{
        module_directory() + L"\\awfan.exe",
        L"broker-restart"
    };
    auto pointers = argument_pointers(arguments);
    const auto response = awfan::request_broker(
        static_cast<int>(pointers.size()),
        pointers.data()
    );
    if (!response.has_value()) {
        throw std::runtime_error("The broker is not currently reachable.");
    }
    std::cout << response->output;
    if (response->exit_code != 0) {
        return response->exit_code;
    }
    for (int attempt = 0; attempt < 40; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (awfan::broker_is_available()) {
            std::cout << "Broker restarted successfully.\n";
            return 0;
        }
    }
    throw std::runtime_error("The broker did not become ready after restart.");
}

int run_broker_repair() {
    const std::wstring directory = module_directory();
    const std::wstring installer = directory + L"\\install.ps1";
    if (GetFileAttributesW(installer.c_str()) == INVALID_FILE_ATTRIBUTES) {
        throw std::runtime_error(
            "install.ps1 is missing. Reinstall awfan from the latest release package."
        );
    }
    const std::wstring parameters =
        L"-NoProfile -ExecutionPolicy Bypass -File " + quote_argument(installer);
    SHELLEXECUTEINFOW operation{};
    operation.cbSize = sizeof(operation);
    operation.fMask = SEE_MASK_NOCLOSEPROCESS;
    operation.lpVerb = L"runas";
    operation.lpFile = L"powershell.exe";
    operation.lpParameters = parameters.c_str();
    operation.lpDirectory = directory.c_str();
    operation.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&operation)) {
        const DWORD code = GetLastError();
        if (code == ERROR_CANCELLED) {
            throw std::runtime_error("Broker repair was cancelled.");
        }
        throw windows_error("Starting broker repair", code);
    }
    Handle process(operation.hProcess);
    WaitForSingleObject(process.get(), INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.get(), &exit_code);
    if (exit_code != 0) {
        throw std::runtime_error(
            "Broker repair failed with exit code " + std::to_string(exit_code) + "."
        );
    }
    std::cout << "Broker repair completed.\n";
    return 0;
}

std::string registry_string(const wchar_t* name) {
    wchar_t buffer[512]{};
    DWORD size = sizeof(buffer);
    const LSTATUS result = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        name,
        RRF_RT_REG_SZ,
        nullptr,
        buffer,
        &size
    );
    return result == ERROR_SUCCESS ? awfan::utf8_from_wide(buffer) : "unknown";
}

std::optional<DWORD> registry_dword(const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS result = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        name,
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size
    );
    if (result != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return value;
}

std::string default_report_name() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t current = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &current);
    std::ostringstream output;
    output << "awfan-report-" << std::put_time(&local, "%Y%m%d-%H%M%S")
           << ".json";
    return output.str();
}

std::string require_json_result(
    const std::vector<std::wstring>& arguments
) {
    const auto result = execute_underlying(arguments);
    if (result.exit_code != 0) {
        throw std::runtime_error(
            "Diagnostic command failed: " + trim(result.output)
        );
    }
    return awfan::redact_diagnostic_text(trim(result.output));
}

int generate_report(const std::optional<std::wstring>& requested_path) {
    fs::path output_path = requested_path.has_value()
        ? fs::path(*requested_path)
        : fs::current_path() / awfan::wide_from_utf8(default_report_name());
    if (output_path.extension().empty()) {
        output_path += L".json";
    }
    output_path = fs::absolute(output_path);

    const BrokerInfo broker = collect_broker_info();
    const std::string doctor = require_json_result({L"doctor", L"--json"});
    const std::string status = require_json_result({L"status", L"--json"});
    const std::string profiles = require_json_result({L"profiles", L"--json"});
    auto state_result = run_core_capture({L"state", L"--json"});
    if (state_result.exit_code != 0) {
        throw std::runtime_error("Could not collect local awfan state.");
    }
    const std::string state = awfan::redact_diagnostic_text(
        trim(state_result.output)
    );

    const std::string product = registry_string(L"ProductName");
    const std::string display_version = registry_string(L"DisplayVersion");
    const std::string build = registry_string(L"CurrentBuildNumber");
    const auto ubr = registry_dword(L"UBR");
    const auto logs = awfan::tail_broker_log(50);

    fs::create_directories(output_path.parent_path());
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create the diagnostic report.");
    }

    output
        << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"awfanVersion\": \"1.2.0\",\n"
        << "  \"privacy\": \"Computer names, usernames, and user paths are redacted.\",\n"
        << "  \"windows\": {\"product\": \""
        << awfan::json_escape_text(product)
        << "\", \"displayVersion\": \""
        << awfan::json_escape_text(display_version)
        << "\", \"build\": \""
        << awfan::json_escape_text(build)
        << "\", \"ubr\": ";
    if (ubr.has_value()) {
        output << *ubr;
    } else {
        output << "null";
    }
    output
        << "},\n"
        << "  \"broker\": {\"reachable\": "
        << (broker.reachable ? "true" : "false")
        << ", \"pid\": ";
    if (broker.process_id.has_value()) {
        output << *broker.process_id;
    } else {
        output << "null";
    }
    output << ", \"uptimeSeconds\": ";
    if (broker.uptime_seconds.has_value()) {
        output << *broker.uptime_seconds;
    } else {
        output << "null";
    }
    output
        << ", \"taskRegistered\": "
        << (broker.task_registered ? "true" : "false")
        << ", \"frontendVersion\": \""
        << awfan::json_escape_text(broker.frontend_version)
        << "\", \"coreVersion\": \""
        << awfan::json_escape_text(broker.core_version)
        << "\", \"brokerVersion\": \""
        << awfan::json_escape_text(broker.broker_version)
        << "\"},\n"
        << "  \"doctor\": " << doctor << ",\n"
        << "  \"status\": " << status << ",\n"
        << "  \"profiles\": " << profiles << ",\n"
        << "  \"state\": " << state << ",\n"
        << "  \"recentBrokerLog\": [";
    for (std::size_t index = 0; index < logs.size(); ++index) {
        output << (index == 0 ? "\n" : ",\n")
               << "    \""
               << awfan::json_escape_text(
                    awfan::redact_diagnostic_text(logs[index])
               )
               << "\"";
    }
    if (!logs.empty()) {
        output << '\n';
    }
    output << "  ]\n}\n";

    std::cout << "Created redacted diagnostic report:\n  "
              << awfan::utf8_from_wide(output_path.wstring()) << "\n";
    return 0;
}

void print_presets(const bool json) {
    const auto presets = awfan::load_presets();
    if (json) {
        std::cout << "{\"presets\": [";
        for (std::size_t index = 0; index < presets.size(); ++index) {
            const auto& preset = presets[index];
            std::cout << (index == 0 ? "" : ", ")
                      << "{\"name\": \""
                      << awfan::json_escape_text(preset.name)
                      << "\", \"cpu\": " << preset.cpu_value
                      << ", \"gpu\": " << preset.gpu_value << "}";
        }
        std::cout << "]}\n";
        return;
    }
    if (presets.empty()) {
        std::cout << "No custom presets saved.\n";
        return;
    }
    std::cout << "Custom presets:\n";
    for (const auto& preset : presets) {
        std::cout << "  " << preset.name
                  << "  CPU " << preset.cpu_value
                  << "  GPU " << preset.gpu_value << '\n';
    }
}

int run_preset_command(
    const std::vector<std::wstring>& arguments
) {
    if (arguments.empty() || arguments.front() == L"list") {
        print_presets(has_argument(arguments, L"--json"));
        return 0;
    }
    const std::wstring action = arguments.front();
    if (action == L"create") {
        if (arguments.size() != 4) {
            throw std::runtime_error(
                "Usage: awfan preset create <name> <cpu> <gpu>"
            );
        }
        const std::string name = awfan::utf8_from_wide(arguments[1]);
        const auto cpu = parse_integer(arguments[2]);
        const auto gpu = parse_integer(arguments[3]);
        if (!awfan::valid_preset_name(name)) {
            throw std::runtime_error(
                "Preset names may contain letters, numbers, hyphens, and underscores."
            );
        }
        if (!cpu.has_value() || !gpu.has_value()
            || *cpu < 0 || *cpu > 100 || *gpu < 0 || *gpu > 100) {
            throw std::runtime_error("Preset boost values must be from 0 to 100.");
        }
        auto presets = awfan::load_presets();
        const auto existing = std::find_if(
            presets.begin(),
            presets.end(),
            [&](const awfan::Preset& preset) { return preset.name == name; }
        );
        if (existing == presets.end()) {
            presets.push_back({name, *cpu, *gpu});
        } else {
            existing->cpu_value = *cpu;
            existing->gpu_value = *gpu;
        }
        awfan::save_presets(presets);
        std::cout << "Saved preset '" << name << "'.\n";
        return 0;
    }
    if (action == L"delete") {
        if (arguments.size() != 2) {
            throw std::runtime_error("Usage: awfan preset delete <name>");
        }
        const std::string name = awfan::utf8_from_wide(arguments[1]);
        auto presets = awfan::load_presets();
        const auto old_size = presets.size();
        presets.erase(
            std::remove_if(
                presets.begin(),
                presets.end(),
                [&](const awfan::Preset& preset) { return preset.name == name; }
            ),
            presets.end()
        );
        if (presets.size() == old_size) {
            throw std::runtime_error("Preset '" + name + "' was not found.");
        }
        awfan::save_presets(presets);
        std::cout << "Deleted preset '" << name << "'.\n";
        return 0;
    }

    std::size_t name_index = 0;
    std::size_t option_index = 1;
    if (action == L"apply") {
        if (arguments.size() < 2) {
            throw std::runtime_error("Usage: awfan preset apply <name> --yes");
        }
        name_index = 1;
        option_index = 2;
    }
    const std::string name = awfan::utf8_from_wide(arguments[name_index]);
    const auto preset = awfan::find_preset(awfan::load_presets(), name);
    if (!preset.has_value()) {
        throw std::runtime_error("Preset '" + name + "' was not found.");
    }
    std::vector<std::wstring> boost_arguments{
        L"boost",
        std::to_wstring(preset->cpu_value),
        std::to_wstring(preset->gpu_value)
    };
    boost_arguments.insert(
        boost_arguments.end(),
        arguments.begin() + static_cast<std::ptrdiff_t>(option_index),
        arguments.end()
    );
    auto result = execute_manual_command(boost_arguments);
    std::cout << result.output;
    return result.exit_code;
}

void print_mode(const bool json) {
    const ProfileSnapshot snapshot = query_profiles();
    const int raw = snapshot.current_raw.value_or(-1);
    int index = -1;
    if (snapshot.current_raw.has_value()) {
        const auto found = std::find(
            snapshot.profiles.begin(),
            snapshot.profiles.end(),
            *snapshot.current_raw
        );
        if (found != snapshot.profiles.end()) {
            index = static_cast<int>(std::distance(snapshot.profiles.begin(), found));
        }
    }
    const std::string name = snapshot.current_raw.has_value()
        ? awfan::profile_name_from_raw(raw)
        : "Unavailable";
    if (json) {
        std::cout
            << "{\"name\": \"" << awfan::json_escape_text(name)
            << "\", \"index\": ";
        if (index >= 0) {
            std::cout << index;
        } else {
            std::cout << "null";
        }
        std::cout << ", \"raw\": ";
        if (snapshot.current_raw.has_value()) {
            std::cout << raw;
        } else {
            std::cout << "null";
        }
        std::cout << ", \"control\": \""
                  << awfan::json_escape_text(snapshot.control_mode)
                  << "\"}\n";
        return;
    }
    std::cout << name << "\nProfile: ";
    if (index >= 0 && snapshot.current_raw.has_value()) {
        std::cout << index << " / " << hex_profile(raw);
    } else {
        std::cout << "unavailable";
    }
    std::cout << "\nControl: " << snapshot.control_mode << '\n';
    const auto restore = awfan::load_restore_target();
    if (restore.has_value()) {
        std::cout
            << "Restore target: "
            << awfan::profile_name_from_index(restore->profile_index)
            << " (profile " << restore->profile_index << ")\n";
    }
    const auto timer = awfan::load_timer_state();
    if (timer.has_value()) {
        std::cout << "Safety restore: ";
        if (timer->restore_on_start) {
            std::cout << "after Windows restarts";
        } else if (timer->expires_epoch_ms.has_value()) {
            const auto remaining = std::max<std::int64_t>(
                0,
                *timer->expires_epoch_ms - awfan::current_epoch_milliseconds()
            );
            std::cout << "in " << awfan::format_duration(remaining);
        } else {
            std::cout << "pending";
        }
        std::cout << '\n';
    }
}

void print_help() {
    std::wcout
        << L"awfan " << kVersion << L"\n\n"
        << L"Native Alienware fan and thermal CLI for Windows.\n\n"
        << L"Monitoring:\n"
        << L"  awfan status [--json]\n"
        << L"  awfan fans [--json]\n"
        << L"  awfan temps [once|seconds] [--json]\n"
        << L"  awfan watch [seconds]\n"
        << L"  awfan profiles [--json]\n"
        << L"  awfan mode [--json]\n"
        << L"  awfan doctor [--json]\n\n"
        << L"Firmware modes (--yes required when changing):\n"
        << L"  awfan mode <name|1-5> --yes\n"
        << L"  awfan balanced --yes\n"
        << L"  awfan balanced-performance --yes\n"
        << L"  awfan cool --yes\n"
        << L"  awfan quiet --yes\n"
        << L"  awfan performance --yes\n"
        << L"  awfan restore --yes\n\n"
        << L"Manual control and safety timers:\n"
        << L"  awfan boost <cpu> <gpu> --yes [--for 20m|--until-reboot]\n"
        << L"  awfan max --yes [--for 5m|--until-reboot]\n\n"
        << L"Custom presets:\n"
        << L"  awfan preset create <name> <cpu> <gpu>\n"
        << L"  awfan preset list [--json]\n"
        << L"  awfan preset <name> --yes [--for 20m]\n"
        << L"  awfan preset delete <name>\n\n"
        << L"Broker management:\n"
        << L"  awfan broker status [--json]\n"
        << L"  awfan broker restart\n"
        << L"  awfan broker repair\n"
        << L"  awfan broker logs [lines]\n\n"
        << L"Maintenance:\n"
        << L"  awfan report [path]\n"
        << L"  awfan update --check\n"
        << L"  awfan update\n"
        << L"  awfan state [--json]\n"
        << L"  awfan clear-state\n"
        << L"  awfan version\n\n"
        << L"Boost values are firmware inputs, not target percentages or RPMs.\n"
        << L"Every hardware-changing command remains blocked without --yes.\n";
}

int run_frontend(int argc, wchar_t** argv) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    const std::wstring command = argv[1];
    if (command == L"version" || command == L"--version") {
        std::wcout << L"awfan " << kVersion << L"\n";
        return 0;
    }
    if (command == L"help" || command == L"--help" || command == L"-h") {
        print_help();
        return 0;
    }
    if (command == L"update") {
        bool check = false;
        bool force = false;
        for (int index = 2; index < argc; ++index) {
            const std::wstring argument = argv[index];
            if (argument == L"--check") {
                check = true;
            } else if (argument == L"--force") {
                force = true;
            } else {
                throw std::runtime_error("update accepts only --check or --force.");
            }
        }
        if (check && force) {
            throw std::runtime_error("Use either --check or --force, not both.");
        }
        return awfan::run_update(check, force);
    }
    if (command == L"broker-status") {
        if (argc != 2) {
            throw std::runtime_error("broker-status accepts no options.");
        }
        print_broker_status(false);
        return awfan::broker_is_available() ? 0 : 1;
    }
    if (command == L"broker") {
        const std::wstring action = argc >= 3 ? argv[2] : L"status";
        if (action == L"status") {
            if (argc > 4 || (argc == 4 && std::wstring(argv[3]) != L"--json")) {
                throw std::runtime_error("Usage: awfan broker status [--json]");
            }
            print_broker_status(argc == 4);
            return awfan::broker_is_available() ? 0 : 1;
        }
        if (action == L"restart" && argc == 3) {
            return run_broker_restart();
        }
        if (action == L"repair" && argc == 3) {
            return run_broker_repair();
        }
        if (action == L"logs") {
            std::size_t lines = 50;
            if (argc == 4) {
                const auto parsed = parse_integer(argv[3]);
                if (!parsed.has_value() || *parsed < 1 || *parsed > 1000) {
                    throw std::runtime_error("Log line count must be from 1 to 1000.");
                }
                lines = static_cast<std::size_t>(*parsed);
            } else if (argc > 4) {
                throw std::runtime_error("Usage: awfan broker logs [lines]");
            }
            const auto log_lines = awfan::tail_broker_log(lines);
            if (log_lines.empty()) {
                std::cout << "No broker log entries are available.\n";
            } else {
                for (const auto& line : log_lines) {
                    std::cout << line << '\n';
                }
            }
            return 0;
        }
        throw std::runtime_error(
            "Usage: awfan broker <status|restart|repair|logs>"
        );
    }
    if (command == L"report") {
        if (argc > 3) {
            throw std::runtime_error("Usage: awfan report [path]");
        }
        return generate_report(argc == 3
            ? std::optional<std::wstring>(argv[2])
            : std::nullopt);
    }
    if (command == L"preset") {
        std::vector<std::wstring> arguments;
        for (int index = 2; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        return run_preset_command(arguments);
    }
    if (command == L"mode") {
        if (argc == 2 || (argc == 3 && std::wstring(argv[2]) == L"--json")) {
            print_mode(argc == 3);
            return 0;
        }
        const int profile_index = awfan::profile_index_from_name(argv[2]);
        if (profile_index == 0) {
            throw std::runtime_error(
                "Mode must be balanced, balanced-performance, cool, quiet, "
                "performance, or an index from 1 to 5."
            );
        }
        std::vector<std::wstring> options;
        for (int index = 3; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (option != L"--yes" && option != L"--json") {
                throw std::runtime_error("mode accepts only --yes and --json.");
            }
            options.push_back(option);
        }
        auto result = execute_profile_command(profile_index, options);
        std::cout << result.output;
        return result.exit_code;
    }
    const int alias_profile = awfan::profile_index_from_name(command);
    if (alias_profile != 0) {
        std::vector<std::wstring> options;
        for (int index = 2; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (option != L"--yes" && option != L"--json") {
                throw std::runtime_error(
                    "Profile aliases accept only --yes and --json."
                );
            }
            options.push_back(option);
        }
        auto result = execute_profile_command(alias_profile, options);
        std::cout << result.output;
        return result.exit_code;
    }
    if (command == L"restore") {
        std::vector<std::wstring> options;
        for (int index = 2; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (option != L"--yes" && option != L"--json") {
                throw std::runtime_error("restore accepts only --yes and --json.");
            }
            options.push_back(option);
        }
        const auto target = awfan::load_restore_target();
        if (!target.has_value()) {
            throw std::runtime_error(
                "No previous firmware profile is remembered. Select a named "
                "profile before entering manual mode."
            );
        }
        auto result = execute_profile_command(target->profile_index, options);
        if (result.exit_code == 0
            && !has_argument(options, L"--json")) {
            std::cout << "Restored "
                      << awfan::profile_name_from_index(target->profile_index)
                      << ".\n";
        }
        std::cout << result.output;
        return result.exit_code;
    }
    if (command == L"boost" || command == L"max") {
        std::vector<std::wstring> arguments;
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        auto result = execute_manual_command(arguments);
        std::cout << result.output;
        return result.exit_code;
    }
    if (command == L"profile" || command == L"auto") {
        if (argc < 3) {
            throw std::runtime_error("Usage: awfan profile <1-5> --yes");
        }
        const auto profile_index = parse_integer(argv[2]);
        if (!profile_index.has_value() || *profile_index < 1 || *profile_index > 5) {
            throw std::runtime_error("Profile index must be from 1 to 5.");
        }
        std::vector<std::wstring> options;
        for (int index = 3; index < argc; ++index) {
            options.emplace_back(argv[index]);
        }
        auto result = execute_profile_command(*profile_index, options);
        std::cout << result.output;
        return result.exit_code;
    }

    std::vector<std::wstring> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    auto result = execute_underlying(arguments);
    std::cout << result.output;
    return result.exit_code;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        return run_frontend(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "awfan: " << error.what() << '\n';
        return 1;
    }
}
