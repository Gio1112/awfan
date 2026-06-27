#include "awfan/broker.hpp"

#include <windows.h>

#include <cwchar>
#include <string>

namespace {

void write_stdout(const std::string& value) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(
        output,
        value.data(),
        static_cast<DWORD>(value.size()),
        &written,
        nullptr
    );
}

}  // namespace

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR command_line,
    int
) {
    if (command_line != nullptr && std::wcscmp(command_line, L"--self-test") == 0) {
        return awfan::run_broker_self_test();
    }

    if (command_line != nullptr && std::wcscmp(command_line, L"--version") == 0) {
        write_stdout("awfan-broker 1.2.0\n");
        return 0;
    }

    DWORD previous_pid = 0;
    if (command_line != nullptr
        && std::swscanf(command_line, L"--restart-helper %lu", &previous_pid) == 1
        && previous_pid != 0) {
        const HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, previous_pid);
        if (previous != nullptr) {
            WaitForSingleObject(previous, 15000);
            CloseHandle(previous);
        }
        Sleep(250);
    }

    try {
        return awfan::run_broker_server();
    } catch (...) {
        return 1;
    }
}
