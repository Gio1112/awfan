#include "awfan/broker.hpp"

#include <windows.h>

#include <cwchar>

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR command_line,
    int
) {
    if (command_line != nullptr && std::wcscmp(command_line, L"--self-test") == 0) {
        return awfan::run_broker_self_test();
    }

    try {
        return awfan::run_broker_server();
    } catch (...) {
        return 1;
    }
}
