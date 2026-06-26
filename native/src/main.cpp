#include "awfan/awcc_exact.hpp"
#include "awfan/awcc_raw.hpp"
#include "awfan/awcc_read.hpp"
#include "awfan/wmi_probe.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void print_help() {
    std::wcout
        << L"awfan-native 0.3.5-diagnostic\n\n"
        << L"Native Alienware WMI backend experiment.\n"
        << L"This build cannot change fan speeds, power profiles, TCC, G-Mode,\n"
        << L"XMP or any other hardware state.\n\n"
        << L"Usage:\n"
        << L"  awfan-native exact-probe\n"
        << L"  awfan-native raw-probe\n"
        << L"  awfan-native status [--json]\n"
        << L"  awfan-native probe [options]\n"
        << L"  awfan-native inspect-awcc [--namespace <path>]\n"
        << L"  awfan-native version\n\n"
        << L"exact-probe:\n"
        << L"  Performs one read-only system-ID call using the confirmed AWCC flow.\n\n"
        << L"raw-probe:\n"
        << L"  Enumerates firmware resources and reads fan, temperature and power\n"
        << L"  values. Provider end-of-list failures are treated as terminators.\n\n"
        << L"status:\n"
        << L"  Reads power, fan and thermal information. This remains experimental.\n\n"
        << L"Probe options:\n"
        << L"  --namespace <path>  Probe one namespace instead of the defaults.\n"
        << L"  --all               Print every WMI class found.\n"
        << L"  --signatures        Print method input/output parameter names.\n"
        << L"  --help              Show this help.\n";
}

int run_exact_probe_command() {
    try {
        return awfan::run_awcc_exact_probe();
    } catch (const std::exception& error) {
        std::cerr << "Exact AWCC probe failed: " << error.what() << '\n';
        return 1;
    }
}

int run_raw_probe_command() {
    try {
        return awfan::run_awcc_raw_probe();
    } catch (const std::exception& error) {
        std::cerr << "Raw AWCC probe failed: " << error.what() << '\n';
        return 1;
    }
}

int run_status_command(int argc, wchar_t** argv) {
    bool json_output = false;

    for (int i = 2; i < argc; ++i) {
        const std::wstring argument = argv[i];

        if (argument == L"--json") {
            json_output = true;
            continue;
        }

        if (argument == L"--help" || argument == L"-h") {
            print_help();
            return 0;
        }

        std::wcerr << L"Unknown status option: " << argument << L"\n";
        return 2;
    }

    try {
        return awfan::run_awcc_read_status(json_output);
    } catch (const std::exception& error) {
        std::cerr << "Native status failed: " << error.what() << '\n';
        return 1;
    }
}

int run_probe_command(int argc, wchar_t** argv) {
    awfan::ProbeOptions options;
    options.namespaces = {L"ROOT\\WMI", L"ROOT\\CIMV2"};

    for (int i = 2; i < argc; ++i) {
        const std::wstring argument = argv[i];

        if (argument == L"--help" || argument == L"-h") {
            print_help();
            return 0;
        }

        if (argument == L"--all") {
            options.show_all_classes = true;
            continue;
        }

        if (argument == L"--signatures") {
            options.show_signatures = true;
            continue;
        }

        if (argument == L"--namespace") {
            if (i + 1 >= argc) {
                std::wcerr << L"Missing value after --namespace.\n";
                return 2;
            }

            options.namespaces = {argv[++i]};
            continue;
        }

        std::wcerr << L"Unknown option: " << argument << L"\n";
        return 2;
    }

    try {
        const auto summary = awfan::run_wmi_probe(options);

        std::wcout
            << L"\nProbe complete: "
            << summary.matching_classes << L" matching class(es) across "
            << summary.namespaces_succeeded << L" namespace(s).\n";

        return summary.namespaces_succeeded == 0 ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Probe failed: " << error.what() << '\n';
        return 1;
    }
}

int run_inspect_awcc_command(int argc, wchar_t** argv) {
    std::wstring namespace_path = L"ROOT\\WMI";

    for (int i = 2; i < argc; ++i) {
        const std::wstring argument = argv[i];

        if (argument == L"--help" || argument == L"-h") {
            print_help();
            return 0;
        }

        if (argument == L"--namespace") {
            if (i + 1 >= argc) {
                std::wcerr << L"Missing value after --namespace.\n";
                return 2;
            }

            namespace_path = argv[++i];
            continue;
        }

        std::wcerr << L"Unknown option: " << argument << L"\n";
        return 2;
    }

    try {
        const auto summary = awfan::run_awcc_instance_probe(namespace_path);

        std::wcout
            << L"\nAWCC inspection complete: class="
            << (summary.class_found ? L"found" : L"missing")
            << L", static methods=" << summary.static_methods
            << L", instances=" << summary.instances_found << L".\n";

        return summary.class_found ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "AWCC inspection failed: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    const std::wstring command = argv[1];

    if (command == L"exact-probe") {
        return run_exact_probe_command();
    }

    if (command == L"raw-probe") {
        return run_raw_probe_command();
    }

    if (command == L"status" || command == L"read-status") {
        return run_status_command(argc, argv);
    }

    if (command == L"probe") {
        return run_probe_command(argc, argv);
    }

    if (command == L"inspect-awcc") {
        return run_inspect_awcc_command(argc, argv);
    }

    if (command == L"version" || command == L"--version") {
        std::wcout << L"awfan-native 0.3.5-diagnostic\n";
        return 0;
    }

    if (command == L"help" || command == L"--help" || command == L"-h") {
        print_help();
        return 0;
    }

    std::wcerr << L"Unknown command: " << command << L"\n\n";
    print_help();
    return 2;
}
