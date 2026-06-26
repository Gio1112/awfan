#pragma once

#include <string>
#include <vector>

namespace awfan {

struct ProbeOptions {
    std::vector<std::wstring> namespaces;
    bool show_all_classes{false};
    bool show_signatures{false};
};

struct ProbeSummary {
    int namespaces_succeeded{0};
    int matching_classes{0};
};

ProbeSummary run_wmi_probe(const ProbeOptions& options);

}  // namespace awfan
