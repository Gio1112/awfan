#include "awfan/awcc_exact.hpp"
#include "awfan/awcc_raw.hpp"
#include "awfan/native_cli.hpp"
#include "awfan/update.hpp"
#include "awfan/wmi_probe.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kVersion[] = L"1.1.1";

void print_presets() {
    std::wcout
        << L"Known USTT profile names for the discovered AC16251 IDs:\n"
        << L"  1  0xA0  Balanced\n"
        << L"  2  0xA1  Balanced Performance\n"
        << L"  3  0xA2  Cool\n"
        << L"  4  0xA3  Quiet\n"
        << L"  5  0xA4  Performance\n\n"
        << L"The raw ID is the source of truth. Run 'awfan profiles' first.\n";
}

void print_help() {
    std::wcout
        << L"awfan " << kVersion << L"\n\n"
        << L"Native C++20 Alienware fan-control CLI.\n\n"
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
        << L"  awfan profile <1-5> --yes [--json]\n"
        << L"  awfan auto <1-5> --yes [--json]\n\n"
        << L"Updates:\n"
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
        << L"Important:\n"
        << L"  - boost values are firmware fan-boost inputs, not target fan\n"
        << L"    percentages or target RPM values.\n"
        << L"  - boost enters manual control. Use profile/auto 1-5 to return to\n"
        << L"    dynamic firmware control.\n"
        << L"  - profile 0 is diagnostic-only because it did not reliably clear\n"
        << L"    an existing manual boost on the tested system.\n"
        << L"  - every hardware write is blocked unless --yes is supplied.\n";
}

bool has_flag(int argc, wchar_t** argv, const std::wstring& flag) {
    for (int index = 2; index < argc; ++index) {
        if (argv[index] == flag) {
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> positional_arguments(int argc, wchar_t** argv) {
    std::vector<std::wstring> values;
    for (int index = 2; index < argc; ++index) {
        const std::wstring value = argv[index];
        if (value != L"--json"
            && value != L"--yes"
            && value != L"--check"
            && value != L"--force") {
            values.push_back(value);
        }
    }
    return values;
}

int parse_integer(
    const std::wstring& value,
    const std::string& name,
    int minimum,
    int maximum
) {
    std::size_t consumed = 0;
    int result = 0;

    try {
        result = std::stoi(value, &consumed, 10);
    } catch (...) {
        throw std::runtime_error(name + " must be an integer.");
    }

    if (consumed != value.size()) {
        throw std::runtime_error(name + " must be an integer.");
    }

    if (result < minimum || result > maximum) {
        throw std::runtime_error(
            name + " must be from " + std::to_string(minimum) + " to " +
            std::to_string(maximum) + "."
        );
    }

    return result;
}

void require_no_values(
    const std::vector<std::wstring>& values,
    const std::string& command
) {
    if (!values.empty()) {
        throw std::runtime_error(command + " does not accept positional arguments.");
    }
}

int run_probe(int argc, wchar_t** argv) {
    awfan::ProbeOptions options;
    options.namespaces = {L"ROOT\\WMI", L"ROOT\\CIMV2"};

    for (int index = 2; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--all") {
            options.show_all_classes = true;
        } else if (argument == L"--signatures") {
            options.show_signatures = true;
        } else if (argument == L"--namespace") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value after --namespace.");
            }
            options.namespaces = {argv[++index]};
        } else {
            throw std::runtime_error("Unknown probe option.");
        }
    }

    const auto summary = awfan::run_wmi_probe(options);
    std::wcout
        << L"\nProbe complete: " << summary.matching_classes
        << L" matching class(es) across " << summary.namespaces_succeeded
        << L" namespace(s).\n";
    return summary.namespaces_succeeded == 0 ? 1 : 0;
}

int run_inspect(int argc, wchar_t** argv) {
    std::wstring namespace_path = L"ROOT\\WMI";

    for (int index = 2; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--namespace") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value after --namespace.");
            }
            namespace_path = argv[++index];
        } else {
            throw std::runtime_error("Unknown inspect-awcc option.");
        }
    }

    const auto summary = awfan::run_awcc_instance_probe(namespace_path);
    std::wcout
        << L"\nAWCC inspection complete: class="
        << (summary.class_found ? L"found" : L"missing")
        << L", static methods=" << summary.static_methods
        << L", instances=" << summary.instances_found << L".\n";
    return summary.class_found ? 0 : 1;
}

int run_command(int argc, wchar_t** argv) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    const std::wstring command = argv[1];
    const bool json = has_flag(argc, argv, L"--json");
    const bool confirmed = has_flag(argc, argv, L"--yes");
    const auto values = positional_arguments(argc, argv);

    if (command == L"status") {
        require_no_values(values, "status");
        return awfan::run_native_status(json);
    }
    if (command == L"fans") {
        require_no_values(values, "fans");
        return awfan::run_native_fans(json);
    }
    if (command == L"profiles" || command == L"profile-list") {
        require_no_values(values, "profiles");
        return awfan::run_native_profiles(json);
    }
    if (command == L"doctor") {
        require_no_values(values, "doctor");
        return awfan::run_native_doctor(json);
    }
    if (command == L"state") {
        require_no_values(values, "state");
        return awfan::run_native_state(json);
    }
    if (command == L"presets") {
        require_no_values(values, "presets");
        if (json || confirmed) {
            throw std::runtime_error("presets accepts no options.");
        }
        print_presets();
        return 0;
    }
    if (command == L"update") {
        require_no_values(values, "update");
        if (json || confirmed) {
            throw std::runtime_error("update accepts only --check or --force.");
        }
        return awfan::run_update(
            has_flag(argc, argv, L"--check"),
            has_flag(argc, argv, L"--force")
        );
    }
    if (command == L"clear-state") {
        require_no_values(values, "clear-state");
        if (json || confirmed) {
            throw std::runtime_error("clear-state accepts no options.");
        }
        return awfan::run_native_clear_state();
    }
    if (command == L"temps") {
        if (values.empty() || values.front() == L"once") {
            if (values.size() > 1) {
                throw std::runtime_error("temps accepts one interval or 'once'.");
            }
            return awfan::run_native_temps(json);
        }
        if (json || values.size() != 1) {
            throw std::runtime_error("Live temperature mode accepts one interval and no --json.");
        }
        return awfan::run_native_watch(
            parse_integer(values.front(), "interval", 1, 60),
            true
        );
    }
    if (command == L"watch") {
        if (json || values.size() > 1) {
            throw std::runtime_error("watch accepts one interval and no --json.");
        }
        const int seconds = values.empty()
            ? 2
            : parse_integer(values.front(), "interval", 1, 60);
        return awfan::run_native_watch(seconds, false);
    }
    if (command == L"boost") {
        if (values.size() != 2) {
            throw std::runtime_error("Usage: awfan boost <cpu-value> <gpu-value> --yes");
        }
        return awfan::run_native_set_boost(
            parse_integer(values[0], "CPU boost value", 0, 100),
            parse_integer(values[1], "GPU boost value", 0, 100),
            confirmed,
            json
        );
    }
    if (command == L"max") {
        require_no_values(values, "max");
        return awfan::run_native_set_boost(100, 100, confirmed, json);
    }
    if (command == L"profile" || command == L"auto") {
        if (values.size() != 1) {
            throw std::runtime_error("Usage: awfan profile <1-5> --yes");
        }
        return awfan::run_native_set_profile(
            parse_integer(values.front(), "profile index", 1, 5),
            confirmed,
            json
        );
    }
    if (command == L"raw-probe" || command == L"diagnose") {
        return awfan::run_awcc_raw_probe();
    }
    if (command == L"exact-probe") {
        return awfan::run_awcc_exact_probe();
    }
    if (command == L"probe") {
        return run_probe(argc, argv);
    }
    if (command == L"inspect-awcc") {
        return run_inspect(argc, argv);
    }
    if (command == L"version" || command == L"--version") {
        std::wcout << L"awfan " << kVersion << L"\n";
        return 0;
    }
    if (command == L"help" || command == L"--help" || command == L"-h") {
        print_help();
        return 0;
    }

    throw std::runtime_error("Unknown command. Run 'awfan help' for usage.");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        return run_command(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "awfan: " << error.what() << '\n';
        return 1;
    }
}
