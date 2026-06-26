#include "awfan/awcc_exact.hpp"
#include "awfan/awcc_raw.hpp"
#include "awfan/native_cli.hpp"
#include "awfan/wmi_probe.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kVersion[] = L"1.0.0-rc1";

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
        << L"  awfan doctor [--json]\n"
        << L"  awfan state [--json]\n\n"
        << L"Control commands (experimental; --yes required):\n"
        << L"  awfan boost <cpu-value> <gpu-value> --yes [--json]\n"
        << L"  awfan max --yes [--json]\n"
        << L"  awfan profile <1-5> --yes [--json]\n"
        << L"  awfan auto <1-5> --yes [--json]\n\n"
        << L"Maintenance and diagnostics:\n"
        << L"  awfan clear-state\n"
        << L"  awfan raw-probe\n"
        << L"  awfan exact-probe\n"
        << L"  awfan probe [--namespace <path>] [--all] [--signatures]\n"
        << L"  awfan inspect-awcc [--namespace <path>]\n"
        << L"  awfan version\n\n"
        << L"Important:\n"
        << L"  - boost values are firmware control inputs from 0 to 100. They are\n"
        << L"    not percentages and do not directly represent target RPM.\n"
        << L"  - boost enters manual control. Use profile/auto 1-5 to clear the\n"
        << L"    manual boost and return to dynamic firmware control.\n"
        << L"  - firmware profile 0 is displayed for diagnostics but is not exposed\n"
        << L"    as a standalone profile command because it does not reliably clear\n"
        << L"    an existing manual boost on the tested system.\n"
        << L"  - status, fans and watch report actual RPM trend by comparing\n"
        << L"    consecutive samples: rising, falling or stable.\n"
        << L"  - reported maximum RPM is nominal. Live telemetry may briefly\n"
        << L"    exceed it.\n"
        << L"  - profiles 1-5 map to discovered firmware profile IDs. Run\n"
        << L"    'awfan profiles' before changing profiles.\n"
        << L"  - every hardware write is blocked unless --yes is supplied.\n";
}

bool has_flag(
    const int argc,
    wchar_t** argv,
    const std::wstring& flag
) {
    for (int index = 2; index < argc; ++index) {
        if (argv[index] == flag) {
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> positional_arguments(
    const int argc,
    wchar_t** argv
) {
    std::vector<std::wstring> values;

    for (int index = 2; index < argc; ++index) {
        const std::wstring value = argv[index];
        if (value == L"--json" || value == L"--yes") {
            continue;
        }
        values.push_back(value);
    }

    return values;
}

int parse_integer(
    const std::wstring& value,
    const std::wstring& name,
    const int minimum,
    const int maximum
) {
    std::size_t consumed = 0;
    int result = 0;

    try {
        result = std::stoi(value, &consumed, 10);
    } catch (...) {
        throw std::runtime_error(
            std::string(name.begin(), name.end()) + " must be an integer."
        );
    }

    if (consumed != value.size()) {
        throw std::runtime_error(
            std::string(name.begin(), name.end()) + " must be an integer."
        );
    }

    if (result < minimum || result > maximum) {
        throw std::runtime_error(
            std::string(name.begin(), name.end()) + " must be from " +
            std::to_string(minimum) + " to " + std::to_string(maximum) + "."
        );
    }

    return result;
}

int run_probe_command(int argc, wchar_t** argv) {
    awfan::ProbeOptions options;
    options.namespaces = {L"ROOT\\WMI", L"ROOT\\CIMV2"};

    for (int index = 2; index < argc; ++index) {
        const std::wstring argument = argv[index];

        if (argument == L"--all") {
            options.show_all_classes = true;
            continue;
        }

        if (argument == L"--signatures") {
            options.show_signatures = true;
            continue;
        }

        if (argument == L"--namespace") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value after --namespace.");
            }
            options.namespaces = {argv[++index]};
            continue;
        }

        throw std::runtime_error("Unknown probe option.");
    }

    const auto summary = awfan::run_wmi_probe(options);
    std::wcout
        << L"\nProbe complete: "
        << summary.matching_classes << L" matching class(es) across "
        << summary.namespaces_succeeded << L" namespace(s).\n";

    return summary.namespaces_succeeded == 0 ? 1 : 0;
}

int run_inspect_command(int argc, wchar_t** argv) {
    std::wstring namespace_path = L"ROOT\\WMI";

    for (int index = 2; index < argc; ++index) {
        const std::wstring argument = argv[index];

        if (argument == L"--namespace") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value after --namespace.");
            }
            namespace_path = argv[++index];
            continue;
        }

        throw std::runtime_error("Unknown inspect-awcc option.");
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
    const bool json_output = has_flag(argc, argv, L"--json");
    const bool confirmed = has_flag(argc, argv, L"--yes");
    const std::vector<std::wstring> values = positional_arguments(argc, argv);

    if (command == L"status") {
        if (!values.empty()) {
            throw std::runtime_error("status accepts only --json.");
        }
        return awfan::run_native_status(json_output);
    }

    if (command == L"fans") {
        if (!values.empty()) {
            throw std::runtime_error("fans accepts only --json.");
        }
        return awfan::run_native_fans(json_output);
    }

    if (command == L"profiles" || command == L"profile-list") {
        if (!values.empty()) {
            throw std::runtime_error("profiles accepts only --json.");
        }
        return awfan::run_native_profiles(json_output);
    }

    if (command == L"doctor") {
        if (!values.empty()) {
            throw std::runtime_error("doctor accepts only --json.");
        }
        return awfan::run_native_doctor(json_output);
    }

    if (command == L"state") {
        if (!values.empty()) {
            throw std::runtime_error("state accepts only --json.");
        }
        return awfan::run_native_state(json_output);
    }

    if (command == L"clear-state") {
        if (!values.empty() || json_output || confirmed) {
            throw std::runtime_error("clear-state accepts no options.");
        }
        return awfan::run_native_clear_state();
    }

    if (command == L"temps") {
        if (values.empty() || values[0] == L"once") {
            if (values.size() > 1) {
                throw std::runtime_error("temps accepts one interval or 'once'.");
            }
            return awfan::run_native_temps(json_output);
        }

        if (json_output) {
            throw std::runtime_error("Live temperature mode does not support --json.");
        }

        const int seconds = parse_integer(values[0], L"interval", 1, 60);
        if (values.size() != 1) {
            throw std::runtime_error("temps accepts one refresh interval.");
        }
        return awfan::run_native_watch(seconds, true);
    }

    if (command == L"watch") {
        if (json_output) {
            throw std::runtime_error("watch does not support --json.");
        }
        const int seconds = values.empty()
            ? 2
            : parse_integer(values[0], L"interval", 1, 60);
        if (values.size() > 1) {
            throw std::runtime_error("watch accepts one refresh interval.");
        }
        return awfan::run_native_watch(seconds, false);
    }

    if (command == L"boost") {
        if (values.size() != 2) {
            throw std::runtime_error(
                "Usage: awfan boost <cpu-value> <gpu-value> --yes"
            );
        }

        const int cpu = parse_integer(values[0], L"CPU boost value", 0, 100);
        const int gpu = parse_integer(values[1], L"GPU boost value", 0, 100);
        return awfan::run_native_set_boost(
            cpu,
            gpu,
            confirmed,
            json_output
        );
    }

    if (command == L"max") {
        if (!values.empty()) {
            throw std::runtime_error("max accepts only --yes and --json.");
        }
        return awfan::run_native_set_boost(100, 100, confirmed, json_output);
    }

    if (command == L"profile" || command == L"auto") {
        if (values.size() != 1) {
            throw std::runtime_error(
                "Usage: awfan profile <1-5> --yes"
            );
        }

        const int profile = parse_integer(
            values[0],
            L"profile index",
            1,
            5
        );
        return awfan::run_native_set_profile(
            profile,
            confirmed,
            json_output
        );
    }

    if (command == L"raw-probe" || command == L"diagnose") {
        return awfan::run_awcc_raw_probe();
    }

    if (command == L"exact-probe") {
        return awfan::run_awcc_exact_probe();
    }

    if (command == L"probe") {
        return run_probe_command(argc, argv);
    }

    if (command == L"inspect-awcc") {
        return run_inspect_command(argc, argv);
    }

    if (command == L"version" || command == L"--version") {
        std::wcout << L"awfan " << kVersion << L"\n";
        return 0;
    }

    if (command == L"help" || command == L"--help" || command == L"-h") {
        print_help();
        return 0;
    }

    throw std::runtime_error(
        "Unknown command. Run 'awfan help' for usage."
    );
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
