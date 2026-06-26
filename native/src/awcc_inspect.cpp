#include "awfan/wmi_probe.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <array>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

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

std::wstring variant_to_string(const VARIANT& value) {
    if ((value.vt & VT_ARRAY) != 0) {
        std::wostringstream stream;
        stream << L"<array vt=" << value.vt << L">";
        return stream.str();
    }

    switch (value.vt) {
        case VT_EMPTY:
            return L"<empty>";
        case VT_NULL:
            return L"<null>";
        case VT_BSTR:
            return value.bstrVal == nullptr ? L"" : std::wstring(value.bstrVal);
        case VT_BOOL:
            return value.boolVal == VARIANT_TRUE ? L"true" : L"false";
        case VT_I1:
            return std::to_wstring(value.cVal);
        case VT_UI1:
            return std::to_wstring(value.bVal);
        case VT_I2:
            return std::to_wstring(value.iVal);
        case VT_UI2:
            return std::to_wstring(value.uiVal);
        case VT_I4:
        case VT_INT:
            return std::to_wstring(value.lVal);
        case VT_UI4:
        case VT_UINT:
            return std::to_wstring(value.ulVal);
        case VT_I8:
            return std::to_wstring(value.llVal);
        case VT_UI8:
            return std::to_wstring(value.ullVal);
        case VT_R4:
            return std::to_wstring(value.fltVal);
        case VT_R8:
            return std::to_wstring(value.dblVal);
        default: {
            std::wostringstream stream;
            stream << L"<vt=" << value.vt << L">";
            return stream.str();
        }
    }
}

bool variant_is_true(const VARIANT& value) {
    if (value.vt == VT_BOOL) {
        return value.boolVal == VARIANT_TRUE;
    }

    if (value.vt == VT_I4 || value.vt == VT_INT) {
        return value.lVal != 0;
    }

    if (value.vt == VT_UI4 || value.vt == VT_UINT) {
        return value.ulVal != 0;
    }

    return false;
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

void print_named_property(
    IWbemClassObject* object,
    const wchar_t* property_name
) {
    VARIANT value;
    VariantInit(&value);

    const HRESULT result = object->Get(
        property_name,
        0,
        &value,
        nullptr,
        nullptr
    );

    if (SUCCEEDED(result) && value.vt != VT_NULL && value.vt != VT_EMPTY) {
        std::wcout
            << L"  " << property_name << L": "
            << variant_to_string(value) << L"\n";
    }

    VariantClear(&value);
}

void print_qualifier(
    IWbemQualifierSet* qualifiers,
    const wchar_t* qualifier_name,
    const wchar_t* indent = L"  "
) {
    VARIANT value;
    VariantInit(&value);

    const HRESULT result = qualifiers->Get(
        qualifier_name,
        0,
        &value,
        nullptr
    );

    if (SUCCEEDED(result) && value.vt != VT_NULL && value.vt != VT_EMPTY) {
        std::wcout
            << indent << qualifier_name << L": "
            << variant_to_string(value) << L"\n";
    }

    VariantClear(&value);
}

bool qualifier_is_true(
    IWbemQualifierSet* qualifiers,
    const wchar_t* qualifier_name
) {
    VARIANT value;
    VariantInit(&value);

    const HRESULT result = qualifiers->Get(
        qualifier_name,
        0,
        &value,
        nullptr
    );

    const bool is_true = SUCCEEDED(result) && variant_is_true(value);
    VariantClear(&value);
    return is_true;
}

int inspect_class_definition(
    IWbemServices* services,
    const std::wstring& class_name
) {
    ScopedBstr class_path(class_name);
    ComPtr<IWbemClassObject> class_definition;

    const HRESULT get_result = services->GetObject(
        class_path.get(),
        0,
        nullptr,
        class_definition.GetAddressOf(),
        nullptr
    );

    if (FAILED(get_result)) {
        throw std::runtime_error("AWCC class definition could not be loaded");
    }

    std::wcout << L"[class definition]\n";
    std::wcout << L"  object path: " << class_name << L"\n";

    ComPtr<IWbemQualifierSet> class_qualifiers;
    if (SUCCEEDED(class_definition->GetQualifierSet(class_qualifiers.GetAddressOf()))) {
        print_qualifier(class_qualifiers.Get(), L"Dynamic");
        print_qualifier(class_qualifiers.Get(), L"Provider");
        print_qualifier(class_qualifiers.Get(), L"Singleton");
    }

    HRESULT result = class_definition->BeginMethodEnumeration(0);
    if (FAILED(result)) {
        throw std::runtime_error("AWCC methods could not be enumerated");
    }

    int static_method_count = 0;
    std::wcout << L"\n[method qualifiers]\n";

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
            class_definition->EndMethodEnumeration();
            throw std::runtime_error("AWCC method enumeration failed");
        }

        std::wcout << L"  " << method_name << L"\n";

        ComPtr<IWbemQualifierSet> method_qualifiers;
        if (SUCCEEDED(class_definition->GetMethodQualifierSet(
                method_name,
                method_qualifiers.GetAddressOf()
            ))) {
            if (qualifier_is_true(method_qualifiers.Get(), L"Static")) {
                ++static_method_count;
            }

            print_qualifier(method_qualifiers.Get(), L"Static", L"    ");
            print_qualifier(method_qualifiers.Get(), L"Implemented", L"    ");
            print_qualifier(method_qualifiers.Get(), L"Bypass_GetObject", L"    ");
            print_qualifier(method_qualifiers.Get(), L"Privileges", L"    ");
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
    return static_method_count;
}

void print_non_system_properties(IWbemClassObject* object) {
    HRESULT result = object->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
    if (FAILED(result)) {
        std::wcout << L"  properties: <unable to enumerate>\n";
        return;
    }

    while (true) {
        BSTR property_name = nullptr;
        VARIANT value;
        VariantInit(&value);
        CIMTYPE type = 0;
        LONG flavor = 0;

        result = object->Next(
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
            std::wcout
                << L"  properties: <enumeration failed "
                << hresult_text(result) << L">\n";
            break;
        }

        if (property_name != nullptr) {
            std::wcout
                << L"  property " << property_name << L": "
                << variant_to_string(value) << L"\n";
        }

        VariantClear(&value);
        SysFreeString(property_name);
    }

    object->EndEnumeration();
}

int enumerate_instances(
    IWbemServices* services,
    const std::wstring& namespace_path
) {
    ScopedBstr query_language(L"WQL");
    ScopedBstr query(L"SELECT * FROM AWCCWmiMethodFunction");
    ComPtr<IEnumWbemClassObject> enumerator;

    const HRESULT query_result = services->ExecQuery(
        query_language.get(),
        query.get(),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        enumerator.GetAddressOf()
    );

    if (FAILED(query_result)) {
        throw std::runtime_error("AWCC instance query failed");
    }

    int instances_found = 0;
    std::wcout
        << L"\n[" << namespace_path << L"\\AWCCWmiMethodFunction instances]\n";

    while (true) {
        ComPtr<IWbemClassObject> instance;
        ULONG returned = 0;

        const HRESULT next_result = enumerator->Next(
            WBEM_INFINITE,
            1,
            instance.GetAddressOf(),
            &returned
        );

        if (next_result == WBEM_S_FALSE || returned == 0) {
            break;
        }

        if (FAILED(next_result)) {
            std::wcout
                << L"instance enumeration stopped: "
                << hresult_text(next_result) << L"\n";
            break;
        }

        ++instances_found;
        std::wcout << L"\n[instance " << instances_found << L"]\n";

        static constexpr std::array<const wchar_t*, 7> properties{
            L"__PATH",
            L"__RELPATH",
            L"__CLASS",
            L"__NAMESPACE",
            L"__SERVER",
            L"InstanceName",
            L"Active"
        };

        for (const wchar_t* property : properties) {
            print_named_property(instance.Get(), property);
        }

        print_non_system_properties(instance.Get());
    }

    return instances_found;
}

}  // namespace

InstanceProbeSummary run_awcc_instance_probe(
    const std::wstring& namespace_path
) {
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

    const auto services = connect_namespace(locator.Get(), namespace_path);

    InstanceProbeSummary summary;
    summary.class_found = true;
    summary.static_methods = inspect_class_definition(
        services.Get(),
        L"AWCCWmiMethodFunction"
    );
    summary.instances_found = enumerate_instances(
        services.Get(),
        namespace_path
    );

    if (summary.instances_found == 0) {
        std::wcout << L"No instances found.\n";

        if (summary.static_methods > 0) {
            std::wcout
                << L"This is expected: the provider exposes static class methods.\n"
                << L"Future calls should use the class path "
                << L"AWCCWmiMethodFunction rather than an instance path.\n";
        }
    }

    return summary;
}

}  // namespace awfan
