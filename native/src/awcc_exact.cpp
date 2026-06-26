#include "awfan/awcc_exact.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <cstdint>
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

std::string hresult_text(const HRESULT result) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return stream.str();
}

std::uint32_t pack_argument(
    const std::uint8_t subcommand,
    const std::uint8_t argument1,
    const std::uint8_t argument2
) {
    return static_cast<std::uint32_t>(subcommand)
        | (static_cast<std::uint32_t>(argument1) << 8U)
        | (static_cast<std::uint32_t>(argument2) << 16U);
}

class ComRuntime {
public:
    ComRuntime() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result_)) {
            throw std::runtime_error(
                "CoInitializeEx failed (" + hresult_text(result_) + ")"
            );
        }

        const HRESULT security = CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_NONE,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE,
            nullptr
        );

        if (FAILED(security) && security != RPC_E_TOO_LATE) {
            throw std::runtime_error(
                "CoInitializeSecurity failed (" + hresult_text(security) + ")"
            );
        }
    }

    ~ComRuntime() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

private:
    HRESULT result_{E_FAIL};
};

std::wstring find_first_instance_path(IWbemServices* services) {
    ScopedBstr class_name(L"AWCCWmiMethodFunction");
    ComPtr<IEnumWbemClassObject> enumerator;

    HRESULT result = services->CreateInstanceEnum(
        class_name.get(),
        WBEM_FLAG_FORWARD_ONLY,
        nullptr,
        enumerator.GetAddressOf()
    );

    if (FAILED(result)) {
        throw std::runtime_error(
            "CreateInstanceEnum failed (" + hresult_text(result) + ")"
        );
    }

    ComPtr<IWbemClassObject> instance;
    ULONG returned = 0;
    result = enumerator->Next(
        10000,
        1,
        instance.GetAddressOf(),
        &returned
    );

    if (FAILED(result) || returned == 0 || instance == nullptr) {
        throw std::runtime_error(
            "No AWCC instance returned (" + hresult_text(result) + ")"
        );
    }

    VARIANT path;
    VariantInit(&path);
    result = instance->Get(
        L"__Path",
        0,
        &path,
        nullptr,
        nullptr
    );

    if (FAILED(result) || path.vt != VT_BSTR || path.bstrVal == nullptr) {
        VariantClear(&path);
        throw std::runtime_error(
            "Could not read AWCC __Path (" + hresult_text(result) + ")"
        );
    }

    const std::wstring value = path.bstrVal;
    VariantClear(&path);
    return value;
}

}  // namespace

int run_awcc_exact_probe() {
    ComRuntime runtime;

    ComPtr<IWbemLocator> locator;
    HRESULT result = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(locator.GetAddressOf())
    );

    if (FAILED(result)) {
        throw std::runtime_error(
            "CoCreateInstance failed (" + hresult_text(result) + ")"
        );
    }

    ScopedBstr namespace_name(L"ROOT\\WMI");
    ComPtr<IWbemServices> services;
    result = locator->ConnectServer(
        namespace_name.get(),
        nullptr,
        nullptr,
        nullptr,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT,
        nullptr,
        nullptr,
        services.GetAddressOf()
    );

    if (FAILED(result)) {
        throw std::runtime_error(
            "ConnectServer failed (" + hresult_text(result) + ")"
        );
    }

    ScopedBstr class_name(L"AWCCWmiMethodFunction");
    ComPtr<IWbemClassObject> awcc_class;
    result = services->GetObject(
        class_name.get(),
        0,
        nullptr,
        awcc_class.GetAddressOf(),
        nullptr
    );

    if (FAILED(result)) {
        throw std::runtime_error(
            "GetObject failed (" + hresult_text(result) + ")"
        );
    }

    const std::wstring instance_path = find_first_instance_path(services.Get());

    ComPtr<IWbemClassObject> input_parameters;
    result = awcc_class->GetMethod(
        L"Thermal_Information",
        0,
        input_parameters.GetAddressOf(),
        nullptr
    );

    if (FAILED(result) || input_parameters == nullptr) {
        throw std::runtime_error(
            "GetMethod failed (" + hresult_text(result) + ")"
        );
    }

    VARIANT parameters{VT_I4};
    parameters.uintVal = pack_argument(0x02, 0x02, 0x00);

    result = input_parameters->Put(
        L"arg2",
        0,
        &parameters,
        0
    );

    if (FAILED(result)) {
        throw std::runtime_error(
            "Put(arg2) failed (" + hresult_text(result) + ")"
        );
    }

    ScopedBstr object_path(instance_path.c_str());
    ScopedBstr method_name(L"Thermal_Information");
    ComPtr<IWbemClassObject> output_parameters;

    result = services->ExecMethod(
        object_path.get(),
        method_name.get(),
        0,
        nullptr,
        input_parameters.Get(),
        output_parameters.GetAddressOf(),
        nullptr
    );

    if (FAILED(result)) {
        throw std::runtime_error(
            "Exact ExecMethod failed (" + hresult_text(result) + ")"
        );
    }

    VARIANT output{VT_I4};
    output.intVal = -1;
    result = output_parameters->Get(
        L"argr",
        0,
        &output,
        nullptr,
        nullptr
    );

    if (FAILED(result)) {
        VariantClear(&output);
        throw std::runtime_error(
            "Get(argr) failed (" + hresult_text(result) + ")"
        );
    }

    const int system_id = output.intVal;
    VariantClear(&output);

    std::wcout << L"AWCC instance: " << instance_path << L"\n";
    std::cout
        << "Thermal_Information packed arg2: 0x"
        << std::hex << std::uppercase << pack_argument(0x02, 0x02, 0x00)
        << std::dec << '\n'
        << "System ID result: " << system_id << " (0x"
        << std::hex << std::uppercase << system_id << std::dec << ")\n";

    return 0;
}

}  // namespace awfan
