#include "awfan/broker.hpp"
#include "awfan/update.hpp"

#include <windows.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kVersion[] = L"1.1.3";

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

    [[nodiscard]] HANDLE get() const noexcept {
        return value_;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
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

bool has_option(int argc, wchar_t** argv, const std::wstring& option) {
    for (int index = 2; index < argc; ++index) {
        if (argv[index] == option) {
            return true;
        }
    }
    return false;
}

int profile_alias_index(const std::wstring& command) {
    if (command == L"balanced") {
        return 1;
    }
    if (command == L"balanced-performance"
        || command == L"balancedperformance") {
        return 2;
    }
    if (command == L"cool") {
        return 3;
    }
    if (command == L"quiet") {
        return 4;
    }
    if (command == L"performance") {
        return 5;
    }
    return 0;
}

int run_core(int argc, wchar_t** argv) {
    const std::wstring executable = module_directory() + L"\\awfan-core.exe";
    if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
        throw std::runtime_error(
            "awfan-core.exe is missing. Reinstall awfan from the latest package."
        );
    }

    std::wstring command_line = quote_argument(executable);
    for (int index = 1; index < argc; ++index) {
        command_line += L" ";
        command_line += quote_argument(argv[index]);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            module_directory().c_str(),
            &startup,
            &process
        )) {
        throw windows_error("Starting awfan-core.exe");
    }

    Handle process_handle(process.hProcess);
    Handle thread_handle(process.hThread);
    WaitForSingleObject(process_handle.get(), INFINITE);

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
        throw windows_error("Reading the awfan core result");
    }

    return static_cast<int>(exit_code);
}

int run_profile_alias(
    int argc,
    wchar_t** argv,
    const int profile_index
) {
    std::vector<std::wstring> arguments{
        argv[0],
        L"auto",
        std::to_wstring(profile_index)
    };

    for (int index = 2; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument != L"--yes" && argument != L"--json") {
            throw std::runtime_error(
                "Profile aliases accept only --yes and --json."
            );
        }
        arguments.push_back(argument);
    }

    std::vector<wchar_t*> pointers;
    pointers.reserve(arguments.size());
    for (auto& argument : arguments) {
        pointers.push_back(argument.data());
    }

    const int rewritten_argc = static_cast<int>(pointers.size());
    wchar_t** rewritten_argv = pointers.data();

    if (!awfan::is_process_elevated()) {
        const auto result = awfan::forward_to_broker(
            rewritten_argc,
            rewritten_argv
        );
        if (result.has_value()) {
            return *result;
        }

        throw std::runtime_error(
            "The elevated background broker is unavailable. Re-run install.ps1 "
            "from the latest awfan package, then run 'awfan broker-status'."
        );
    }

    return run_core(rewritten_argc, rewritten_argv);
}

void print_help() {
    std::wcout
        << L"awfan " << kVersion << L"\n\n"
        << L"Native Alienware fan and thermal CLI for Windows.\n\n"
        << L"Read commands:\n"
        << L"  awfan status [--json]\n"
        << L"  awfan fans [--json]\n"
        << L"  awfan temps [once|seconds] [--json]\n"
        << L"  awfan watch [seconds]\n"
        << L"  awfan profiles [--json]\n"
        << L"  awfan presets\n"
        << L"  awfan doctor [--json]\n"
        << L"  awfan state [--json]\n\n"
        << L"Control commands (experimental; --yes required):\n"
        << L"  awfan boost <cpu-value> <gpu-value> --yes [--json]\n"
        << L"  awfan max --yes [--json]\n"
        << L"  awfan profile <0-5> --yes [--json]\n"
        << L"  awfan auto <1-5> --yes [--json]\n\n"
        << L"Profile aliases:\n"
        << L"  awfan balanced --yes             Profile 1 / 0xA0\n"
        << L"  awfan balanced-performance --yes Profile 2 / 0xA1\n"
        << L"  awfan cool --yes                 Profile 3 / 0xA2\n"
        << L"  awfan quiet --yes                Profile 4 / 0xA3\n"
        << L"  awfan performance --yes          Profile 5 / 0xA4\n\n"
        << L"Background broker and updates:\n"
        << L"  awfan broker-status\n"
        << L"  awfan update --check\n"
        << L"  awfan update\n"
        << L"  awfan update --force\n\n"
        << L"Maintenance and diagnostics:\n"
        << L"  awfan clear-state\n"
        << L"  awfan raw-probe\n"
        << L"  awfan exact-probe\n"
        << L"  awfan probe [--namespace <path>] [--all] [--signatures]\n"
        << L"  awfan inspect-awcc [--namespace <path>]\n"
        << L"  awfan version\n\n"
        << L"The installer creates a per-user elevated broker. Hardware commands\n"
        << L"from a normal terminal are sent to that broker without repeated UAC\n"
        << L"prompts. Every hardware write still requires --yes.\n";
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

    if (command == L"broker-status") {
        if (argc != 2) {
            throw std::runtime_error("broker-status accepts no options.");
        }

        const bool available = awfan::broker_is_available();
        std::cout
            << "Background broker: " << (available ? "running" : "unavailable")
            << "\nCurrent process: "
            << (awfan::is_process_elevated() ? "elevated" : "standard user")
            << "\n";
        return available ? 0 : 1;
    }

    if (command == L"update") {
        for (int index = 2; index < argc; ++index) {
            const std::wstring argument = argv[index];
            if (argument != L"--check" && argument != L"--force") {
                throw std::runtime_error("update accepts only --check or --force.");
            }
        }

        return awfan::run_update(
            has_option(argc, argv, L"--check"),
            has_option(argc, argv, L"--force")
        );
    }

    const int alias_profile = profile_alias_index(command);
    if (alias_profile != 0) {
        return run_profile_alias(argc, argv, alias_profile);
    }

    if (awfan::command_requires_broker(command)
        && !awfan::is_process_elevated()) {
        const auto result = awfan::forward_to_broker(argc, argv);
        if (result.has_value()) {
            return *result;
        }

        throw std::runtime_error(
            "The elevated background broker is unavailable. Re-run install.ps1 "
            "from the latest awfan package, then run 'awfan broker-status'."
        );
    }

    return run_core(argc, argv);
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
