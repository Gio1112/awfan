#include "awfan/wmi_probe.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace awfan {
namespace {

using Microsoft::WRL::ComPtr;

class ScopedBstr {
public:
    explicit ScopedBstr(const wchar_t* value) : value_(SysAllocString(value)) {
        if (value_ == nullptr) {
            throw std::bad_alloc();
        }
    }

    explicit ScopedBstr(const std::wstring& value) : ScopedBstr(value.c_str()) {}

    ~ScopedBstr() {
        SysFreeString(value_);
    }

    ScopedBstr(const ScopedBstr&) = delete;
    ScopedBstr& operator=(const ScopedBstr&) = delete;

    [[nodiscard]] BSTR get() const noexcept {
        return value_;
    }

private:
    BSTR value_{nullptr};
};

class ComRuntime {
public:
    ComRuntime() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result_)) {
            throw std::runtime_error("CoInitializeEx failed");
        }

        const HRESULT security_result = CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE,
            nullptr
        );

        if (FAILED(security_result) && security_result != RPC_E_TOO_LATE) {
            CoUninitialize();
            result_ = E_FAIL;
            throw std::runtime_error("CoInitializeSecurity failed");
        }
    }

    ~ComRuntime() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    ComRuntime(const ComRuntime&) = delete;
    ComRuntime& operator=(const ComRuntime&) = delete;

private:
    HRESULT result_{E_FAIL};
};

std::wstring hresult_text(const HRESULT result) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return stream.str();
}

std::wstring lower_copy(std::wstring value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        }
    );
    return value;
}

bool is_interesting_class(const std::wstring& class_name) {
    static constexpr std::array keywords{
        L"awcc",
        L"alien",
        L"dell",
        L"thermal",
        L"temperature",
        L"fan",
        L"sensor",
        L"wmi",
        L"acpi"
    };

    const std::wstring lowered = lower_copy(class_name);

    return std::any_of(
        keywords.begin(),
        keywords.end(),
        [&lowered](const wchar_t* keyword) {
            return lowered.find(keyword) != std::wstring::npos;
        }
    );
}

std::wstring cim_type_name(const CIMTYPE type) {
    const bool is_array = (type & CIM_FLAG_ARRAY) != 0;
    const CIMTYPE base_type = type & ~CIM_FLAG_ARRAY;

    const wchar_t* name = L"unknown";

    switch (base_type) {
        case CIM_SINT8: name = L"sint8"; break;
        case CIM_UINT8: name = L"uint8"; break;
        case CIM_SINT16: name = L"sint16"; break;
        case CIM_UINT16: name = L"uint16"; break;
        case CIM_SINT32: name = L"sint32"; break;
        case CIM_UINT32: name = L"uint32"; break;
        case CIM_SINT64: name = L"sint64"; break;
        case CIM_UINT64: name = L"uint64"; break;
        case CIM_REAL32: name = L"real32"; break;
        case CIM_REAL64: name = L"real64"; break;
        case CIM_BOOLEAN: name = L"boolean"; break;
        case CIM_STRING: name = L"string"; break;
        case CIM_DATETIME: name = L"datetime"; break;
        case CIM_REFERENCE: name = L"reference"; break;
        case CIM_CHAR16: name = L"char16"; break;
        case CIM_OBJECT: name = L"object"; break;
        default: break;
    }

    std::wstring result = name;
    if (is_array) {
        result += L"[]";
    }
    return result;
}

void print_signature(
    const wchar_t* label,
    IWbemClassObject* signature
) {
    if (signature == nullptr) {
        return;
    }

    HRESULT result = signature->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
    if (FAILED(result)) {
        std::wcout << L"      " << label << L": <unable to enumerate>\n";
        return;
    }

    bool printed_label = false;

    while (true) {
        BSTR property_name = nullptr;
        VARIANT value;
        VariantInit(&value);
        CIMTYPE type = 0;
        LONG flavor = 0;

        result = signature->Next(
            0,
            &property_name,
            &value,
            &type,
            &flavor
        );

        if (result == WBEM_S_NO_MORE_DATA || result == WBEM_S_FALSE) {
            VariantClear(&value);
            break;
        }

        if (FAILED(result)) {
            VariantClear(&value);
            SysFreeString(property_name);
            break;
        }

        if (!printed_label) {
            std::wcout << L"      " << label << L":\n";
            printed_label = true;
        }

        std::wcout
            << L"        " << property_name
            << L" : " << cim_type_name(type) << L"\n";

        VariantClear(&value);
        SysFreeString(property_name);
    }

    signature->EndEnumeration();
}

void print_methods(
    IWbemServices* services,
    const std::wstring& class_name,
    const bool show_signatures
) {
    ScopedBstr class_path(class_name);
    ComPtr<IWbemClassObject> class_definition;

    HRESULT result = services->GetObject(
        class_path.get(),
        0,
        nullptr,
        class_definition.GetAddressOf(),
        nullptr
    );

    if (FAILED(result)) {
        std::wcout
            << L"    methods: <class definition unavailable "
            << hresult_text(result) << L">\n";
        return;
    }

    result = class_definition->BeginMethodEnumeration(0);
    if (FAILED(result)) {
        std::wcout << L"    methods: <unable to enumerate>\n";
        return;
    }

    bool found_method = false;

    while (true) {
        BSTR method_name = nullptr;
        IWbemClassObject* input_signature = nullptr;
        IWbemClassObject* output_signature = nullptr;

        result = class_definition->NextMethod(
            0,
            &method_name,
            &input_signature,
            &output_signature
        );

        if (result == WBEM_S_NO_MORE_DATA || result == WBEM_S_FALSE) {
            break;
        }

        if (FAILED(result)) {
            std::wcout
                << L"    methods: <enumeration failed "
                << hresult_text(result) << L">\n";
            break;
        }

        if (!found_method) {
            std::wcout << L"    methods:\n";
            found_method = true;
        }

        std::wcout << L"      " << method_name << L"\n";

        if (show_signatures) {
            print_signature(L"input", input_signature);
            print_signature(L"output", output_signature);
        }

        SysFreeString(method_name);

        if (input_signature != nullptr) {
            input_signature->Release();
        }
        if (output_signature != nullptr) {
            output_signature->Release();
        }
    }

    class_definition->EndMethodEnumeration();

    if (!found_method) {
        std::wcout << L"    methods: none\n";
    }
}

ComPtr<IWbemServices> connect_namespace(
    IWbemLocator* locator,
    const std::wstring& namespace_path
) {
    ScopedBstr namespace_name(namespace_path);
    ComPtr<IWbemServices> services;

    HRESULT result = locator->ConnectServer(
        namespace_name.get(),
        nullptr,
        nullptr,
        nullptr,
        0,
        nullptr,
        nullptr,
        services.GetAddressOf()
    );

    if (FAILED(result)) {
        throw std::runtime_error("ConnectServer failed");
    }

    result = CoSetProxyBlanket(
        services.Get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE
    );

    if (FAILED(result)) {
        throw std::runtime_error("CoSetProxyBlanket failed");
    }

    return services;
}

int probe_namespace(
    IWbemLocator* locator,
    const std::wstring& namespace_path,
    const ProbeOptions& options
) {
    std::wcout << L"\n[" << namespace_path << L"]\n";

    ComPtr<IWbemServices> services;

    try {
        services = connect_namespace(locator, namespace_path);
    } catch (const std::exception& error) {
        std::wcout << L"  unavailable: " << error.what() << L"\n";
        return -1;
    }

    ScopedBstr query_language(L"WQL");
    ScopedBstr query(L"SELECT * FROM meta_class");
    ComPtr<IEnumWbemClassObject> enumerator;

    const HRESULT query_result = services->ExecQuery(
        query_language.get(),
        query.get(),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        enumerator.GetAddressOf()
    );

    if (FAILED(query_result)) {
        std::wcout
            << L"  class enumeration failed: "
            << hresult_text(query_result) << L"\n";
        return -1;
    }

    int match_count = 0;

    while (true) {
        ComPtr<IWbemClassObject> class_object;
        ULONG returned = 0;

        const HRESULT next_result = enumerator->Next(
            WBEM_INFINITE,
            1,
            class_object.GetAddressOf(),
            &returned
        );

        if (next_result == WBEM_S_FALSE || returned == 0) {
            break;
        }

        if (FAILED(next_result)) {
            std::wcout
                << L"  class enumeration stopped: "
                << hresult_text(next_result) << L"\n";
            break;
        }

        VARIANT class_name_value;
        VariantInit(&class_name_value);

        const HRESULT name_result = class_object->Get(
            L"__CLASS",
            0,
            &class_name_value,
            nullptr,
            nullptr
        );

        if (
            FAILED(name_result) ||
            class_name_value.vt != VT_BSTR ||
            class_name_value.bstrVal == nullptr
        ) {
            VariantClear(&class_name_value);
            continue;
        }

        const std::wstring class_name = class_name_value.bstrVal;
        VariantClear(&class_name_value);

        if (!options.show_all_classes && !is_interesting_class(class_name)) {
            continue;
        }

        ++match_count;
        std::wcout << L"  " << class_name << L"\n";
        print_methods(services.Get(), class_name, options.show_signatures);
    }

    if (match_count == 0) {
        std::wcout << L"  no matching classes found\n";
    }

    return match_count;
}

}  // namespace

ProbeSummary run_wmi_probe(const ProbeOptions& options) {
    ComRuntime runtime;

    ComPtr<IWbemLocator> locator;
    const HRESULT locator_result = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(locator.GetAddressOf())
    );

    if (FAILED(locator_result)) {
        throw std::runtime_error("Could not create IWbemLocator");
    }

    ProbeSummary summary;

    for (const auto& namespace_path : options.namespaces) {
        const int matches = probe_namespace(
            locator.Get(),
            namespace_path,
            options
        );

        if (matches >= 0) {
            ++summary.namespaces_succeeded;
            summary.matching_classes += matches;
        }
    }

    return summary;
}

}  // namespace awfan
