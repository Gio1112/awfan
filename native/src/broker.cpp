#include "awfan/broker.hpp"
#include "awfan/platform_features.hpp"

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace awfan {
namespace {

constexpr std::uint32_t kMaximumArguments = 64;
constexpr std::uint32_t kMaximumArgumentCharacters = 8192;
constexpr std::uint32_t kMaximumFrameBytes = 1024U * 1024U;
constexpr wchar_t kBrokerVersion[] = L"1.2.0";

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}

    ~Handle() {
        reset();
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

    [[nodiscard]] HANDLE get() const noexcept {
        return value_;
    }

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

std::runtime_error windows_error(
    const std::string& operation,
    const DWORD code = GetLastError()
) {
    return std::runtime_error(
        operation + " failed with Windows error " + std::to_string(code) + "."
    );
}

std::wstring current_user_sid() {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
        throw windows_error("OpenProcessToken");
    }
    Handle token(raw_token);

    DWORD required = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
    if (required == 0) {
        throw windows_error("GetTokenInformation size query");
    }

    std::vector<unsigned char> storage(required);
    if (!GetTokenInformation(
            token.get(),
            TokenUser,
            storage.data(),
            required,
            &required
        )) {
        throw windows_error("GetTokenInformation");
    }

    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(token_user->User.Sid, &sid_text)) {
        throw windows_error("ConvertSidToStringSidW");
    }

    const std::wstring result = sid_text;
    LocalFree(sid_text);
    return result;
}

std::wstring broker_pipe_name() {
    return L"\\\\.\\pipe\\awfan-broker-" + current_user_sid();
}

std::wstring broker_mutex_name() {
    return L"Local\\awfan-broker-" + current_user_sid();
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

bool command_allowed(const std::wstring& command) {
    static constexpr std::array<std::wstring_view, 17> commands{
        L"status",
        L"fans",
        L"temps",
        L"watch",
        L"profiles",
        L"profile-list",
        L"doctor",
        L"boost",
        L"max",
        L"profile",
        L"auto",
        L"raw-probe",
        L"diagnose",
        L"exact-probe",
        L"probe",
        L"inspect-awcc",
        L"broker-restart"
    };

    return std::find(commands.begin(), commands.end(), command) != commands.end();
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

bool write_all(HANDLE handle, const void* data, std::uint32_t bytes) {
    const auto* cursor = static_cast<const unsigned char*>(data);

    while (bytes > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, cursor, bytes, &written, nullptr) || written == 0) {
            return false;
        }

        cursor += written;
        bytes -= written;
    }

    return true;
}

bool read_all(HANDLE handle, void* data, std::uint32_t bytes) {
    auto* cursor = static_cast<unsigned char*>(data);

    while (bytes > 0) {
        DWORD read = 0;
        if (!ReadFile(handle, cursor, bytes, &read, nullptr) || read == 0) {
            return false;
        }

        cursor += read;
        bytes -= read;
    }

    return true;
}

bool send_frame(HANDLE pipe, const void* data, const std::uint32_t bytes) {
    return write_all(pipe, &bytes, sizeof(bytes))
        && (bytes == 0 || write_all(pipe, data, bytes));
}

void send_result(HANDLE pipe, const DWORD exit_code) {
    const std::uint32_t end_marker = 0;
    const std::uint32_t result = exit_code;
    write_all(pipe, &end_marker, sizeof(end_marker));
    write_all(pipe, &result, sizeof(result));
}

std::vector<std::wstring> receive_request(HANDLE pipe) {
    std::uint32_t count = 0;
    if (!read_all(pipe, &count, sizeof(count))
        || count == 0
        || count > kMaximumArguments) {
        throw std::runtime_error("Invalid broker request argument count.");
    }

    std::vector<std::wstring> arguments;
    arguments.reserve(count);

    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t characters = 0;
        if (!read_all(pipe, &characters, sizeof(characters))
            || characters > kMaximumArgumentCharacters) {
            throw std::runtime_error("Invalid broker request argument length.");
        }

        std::wstring argument(characters, L'\0');
        if (characters > 0 && !read_all(
                pipe,
                argument.data(),
                characters * static_cast<std::uint32_t>(sizeof(wchar_t))
            )) {
            throw std::runtime_error("Incomplete broker request argument.");
        }

        arguments.push_back(std::move(argument));
    }

    if (!command_allowed(arguments.front())) {
        throw std::runtime_error("The broker rejected an unsupported command.");
    }

    return arguments;
}

bool send_request(HANDLE pipe, int argc, wchar_t** argv) {
    if (argc < 2 || argc - 1 > static_cast<int>(kMaximumArguments)) {
        return false;
    }

    const std::uint32_t count = static_cast<std::uint32_t>(argc - 1);
    if (!write_all(pipe, &count, sizeof(count))) {
        return false;
    }

    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument.size() > kMaximumArgumentCharacters) {
            return false;
        }

        const auto characters = static_cast<std::uint32_t>(argument.size());
        if (!write_all(pipe, &characters, sizeof(characters))) {
            return false;
        }

        if (characters > 0 && !write_all(
                pipe,
                argument.data(),
                characters * static_cast<std::uint32_t>(sizeof(wchar_t))
            )) {
            return false;
        }
    }

    return true;
}

std::wstring core_command_line(const std::vector<std::wstring>& arguments) {
    const std::wstring executable = module_directory() + L"\\awfan-core.exe";
    std::wstring command_line = quote_argument(executable);
    for (const auto& argument : arguments) {
        command_line += L" ";
        command_line += quote_argument(argument);
    }
    return command_line;
}

void execute_request(HANDLE client, const std::vector<std::wstring>& arguments) {
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
        throw windows_error("Opening NUL for broker input");
    }

    const std::wstring executable = module_directory() + L"\\awfan-core.exe";
    std::wstring command_line = core_command_line(arguments);

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
            module_directory().c_str(),
            &startup,
            &process
        )) {
        throw windows_error("Starting awfan-core.exe");
    }

    Handle process_handle(process.hProcess);
    Handle thread_handle(process.hThread);
    output_write.reset();

    std::array<unsigned char, 8192> output{};
    bool client_connected = true;

    while (true) {
        DWORD read = 0;
        if (!ReadFile(
                output_read.get(),
                output.data(),
                static_cast<DWORD>(output.size()),
                &read,
                nullptr
            )) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                break;
            }
            throw windows_error("Reading awfan-core output");
        }

        if (read == 0) {
            break;
        }

        if (!send_frame(client, output.data(), read)) {
            client_connected = false;
            TerminateProcess(process_handle.get(), ERROR_CANCELLED);
            break;
        }
    }

    WaitForSingleObject(process_handle.get(), INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process_handle.get(), &exit_code);
    if (client_connected) {
        send_result(client, exit_code);
    }
}

int execute_background_core(const std::vector<std::wstring>& arguments) {
    SECURITY_ATTRIBUTES inherited{};
    inherited.nLength = sizeof(inherited);
    inherited.bInheritHandle = TRUE;

    Handle null_handle(CreateFileW(
        L"NUL",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inherited,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));
    if (!null_handle.valid()) {
        return 1;
    }

    const std::wstring executable = module_directory() + L"\\awfan-core.exe";
    std::wstring command_line = core_command_line(arguments);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_handle.get();
    startup.hStdOutput = null_handle.get();
    startup.hStdError = null_handle.get();

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            module_directory().c_str(),
            &startup,
            &process
        )) {
        return static_cast<int>(GetLastError());
    }

    Handle process_handle(process.hProcess);
    Handle thread_handle(process.hThread);
    WaitForSingleObject(process_handle.get(), INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process_handle.get(), &exit_code);
    return static_cast<int>(exit_code);
}

void process_safety_timer(const bool broker_start) {
    const auto timer = load_timer_state();
    if (!timer.has_value()) {
        return;
    }

    const bool due =
        (broker_start && timer->restore_on_start)
        || (timer->expires_epoch_ms.has_value()
            && current_epoch_milliseconds() >= *timer->expires_epoch_ms);

    if (!due) {
        return;
    }

    append_broker_log(
        "Restoring firmware profile "
        + std::to_string(timer->restore_profile_index)
        + (broker_start ? " after broker start" : " after safety timer")
    );

    const int result = execute_background_core({
        L"auto",
        std::to_wstring(timer->restore_profile_index),
        L"--yes"
    });

    if (result == 0) {
        clear_timer_state();
        append_broker_log("Safety restore completed successfully");
    } else {
        append_broker_log(
            "Safety restore failed with exit code " + std::to_string(result)
        );
    }
}

void timer_loop() {
    while (true) {
        try {
            process_safety_timer(false);
        } catch (const std::exception& error) {
            append_broker_log(std::string("Timer error: ") + error.what());
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void spawn_restart_helper() {
    const std::wstring executable = module_directory() + L"\\awfan-broker.exe";
    std::wstring command_line = quote_argument(executable)
        + L" --restart-helper " + std::to_wstring(GetCurrentProcessId());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(
            executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            module_directory().c_str(),
            &startup,
            &process
        )) {
        throw windows_error("Starting the broker restart helper");
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void serve_client(HANDLE raw_pipe) {
    Handle pipe(raw_pipe);

    try {
        const std::vector<std::wstring> arguments = receive_request(pipe.get());
        append_broker_log(
            "Command: " + utf8_from_wide(arguments.front())
        );

        if (arguments.front() == L"broker-restart") {
            const std::string message = "Broker restart requested.\n";
            send_frame(
                pipe.get(),
                message.data(),
                static_cast<std::uint32_t>(message.size())
            );
            send_result(pipe.get(), 0);
            FlushFileBuffers(pipe.get());
            DisconnectNamedPipe(pipe.get());
            append_broker_log("Restart helper started");
            spawn_restart_helper();
            ExitProcess(0);
        }

        execute_request(pipe.get(), arguments);
    } catch (const std::exception& error) {
        append_broker_log(std::string("Request error: ") + error.what());
        const std::string message = std::string("awfan broker: ")
            + error.what() + "\n";
        send_frame(
            pipe.get(),
            message.data(),
            static_cast<std::uint32_t>(message.size())
        );
        send_result(pipe.get(), 1);
    }

    FlushFileBuffers(pipe.get());
    DisconnectNamedPipe(pipe.get());
}

PSECURITY_DESCRIPTOR create_pipe_security_descriptor() {
    const std::wstring descriptor_text =
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + current_user_sid() + L")";

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            descriptor_text.c_str(),
            SDDL_REVISION_1,
            &descriptor,
            nullptr
        )) {
        throw windows_error("Creating broker pipe security descriptor");
    }

    return descriptor;
}

HANDLE create_pipe_instance() {
    PSECURITY_DESCRIPTOR descriptor = create_pipe_security_descriptor();

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;

    const std::wstring name = broker_pipe_name();
    const HANDLE pipe = CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES,
        64U * 1024U,
        64U * 1024U,
        0,
        &attributes
    );

    LocalFree(descriptor);
    return pipe;
}

}  // namespace

bool is_process_elevated() {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
        return false;
    }
    Handle token(raw_token);

    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    if (!GetTokenInformation(
            token.get(),
            TokenElevation,
            &elevation,
            sizeof(elevation),
            &returned
        )) {
        return false;
    }

    return elevation.TokenIsElevated != 0;
}

bool command_requires_broker(const std::wstring& command) {
    return command_allowed(command);
}

bool broker_is_available() {
    try {
        const std::wstring name = broker_pipe_name();
        if (WaitNamedPipeW(name.c_str(), 100)) {
            return true;
        }
        return GetLastError() == ERROR_PIPE_BUSY;
    } catch (...) {
        return false;
    }
}

std::optional<BrokerResponse> request_broker(int argc, wchar_t** argv) {
    const std::wstring name = broker_pipe_name();
    if (!WaitNamedPipeW(name.c_str(), 3000)) {
        return std::nullopt;
    }

    Handle pipe(CreateFileW(
        name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    ));
    if (!pipe.valid()) {
        return std::nullopt;
    }

    if (!send_request(pipe.get(), argc, argv)) {
        throw std::runtime_error("Could not send the command to the awfan broker.");
    }

    BrokerResponse response;

    while (true) {
        std::uint32_t bytes = 0;
        if (!read_all(pipe.get(), &bytes, sizeof(bytes))) {
            throw std::runtime_error("The awfan broker disconnected unexpectedly.");
        }

        if (bytes == 0) {
            std::uint32_t exit_code = 1;
            if (!read_all(pipe.get(), &exit_code, sizeof(exit_code))) {
                throw std::runtime_error("The awfan broker omitted the command result.");
            }
            response.exit_code = static_cast<int>(exit_code);
            return response;
        }

        if (bytes > kMaximumFrameBytes) {
            throw std::runtime_error("The awfan broker returned an invalid output frame.");
        }

        const std::size_t previous_size = response.output.size();
        response.output.resize(previous_size + bytes);
        if (!read_all(
                pipe.get(),
                response.output.data() + previous_size,
                bytes
            )) {
            throw std::runtime_error("The awfan broker output ended unexpectedly.");
        }
    }
}

std::optional<int> forward_to_broker(int argc, wchar_t** argv) {
    const auto response = request_broker(argc, argv);
    if (!response.has_value()) {
        return std::nullopt;
    }

    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!response->output.empty()
        && !write_all(
            output,
            response->output.data(),
            static_cast<std::uint32_t>(response->output.size())
        )) {
        return 1;
    }

    return response->exit_code;
}

int run_broker_self_test() {
    try {
        const std::wstring sid = current_user_sid();
        if (sid.empty() || broker_pipe_name().empty() || broker_mutex_name().empty()) {
            return 1;
        }

        PSECURITY_DESCRIPTOR descriptor = create_pipe_security_descriptor();
        LocalFree(descriptor);
        return 0;
    } catch (...) {
        return 1;
    }
}

int run_broker_server() {
    if (!is_process_elevated()) {
        return ERROR_ELEVATION_REQUIRED;
    }

    Handle mutex(CreateMutexW(nullptr, TRUE, broker_mutex_name().c_str()));
    const DWORD mutex_error = GetLastError();
    if (!mutex.valid()) {
        return static_cast<int>(mutex_error);
    }
    if (mutex_error == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    append_broker_log(
        "Broker " + utf8_from_wide(kBrokerVersion)
        + " started, PID " + std::to_string(GetCurrentProcessId())
    );

    try {
        process_safety_timer(true);
    } catch (const std::exception& error) {
        append_broker_log(std::string("Startup timer error: ") + error.what());
    }
    std::thread(timer_loop).detach();

    while (true) {
        const HANDLE pipe = create_pipe_instance();
        if (pipe == INVALID_HANDLE_VALUE) {
            append_broker_log(
                "CreateNamedPipe failed with " + std::to_string(GetLastError())
            );
            return static_cast<int>(GetLastError());
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : GetLastError() == ERROR_PIPE_CONNECTED;

        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        std::thread(serve_client, pipe).detach();
    }
}

}  // namespace awfan
