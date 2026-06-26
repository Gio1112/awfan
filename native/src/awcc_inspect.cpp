#include "awfan/wmi_probe.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <array>
#include <iomanip>
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
        std::ostringstream message;
        message << "AWCC instance query failed";
        throw std::runtime_error(message.str());
    }

    InstanceProbeSummary summary;

    std::wcout
        << L"[" << namespace_path << L"\\AWCCWmiMethodFunction instances]\n";

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

        ++summary.instances_found;
        std::wcout << L"\n[instance " << summary.instances_found << L"]\n";

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

    if (summary.instances_found == 0) {
        std::wcout << L"No AWCCWmiMethodFunction instances found.\n";
    }

    return summary;
}

}  // namespace awfan
