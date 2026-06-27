#pragma once

#include <optional>
#include <string>

namespace awfan {

struct BrokerResponse {
    int exit_code{1};
    std::string output;
};

bool is_process_elevated();
bool command_requires_broker(const std::wstring& command);
bool broker_is_available();
std::optional<BrokerResponse> request_broker(int argc, wchar_t** argv);
std::optional<int> forward_to_broker(int argc, wchar_t** argv);
int run_broker_server();
int run_broker_self_test();

}  // namespace awfan
