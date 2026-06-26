#include "awfan/awcc_raw.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <array>
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

private:
    HRESULT result_{E_FAIL};
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

std::array<std::uint8_t, 4> bytes_of(const std::uint32_t value) {
    return {
        static_cast<std::uint8_t>(value & 0xffU),
        static_cast<std::uint8_t>((value >> 8U) & 0xffU),
        static_cast<std::uint8_t>((value >> 16U) & 0xffU),
        static_cast<std::uint8_t>((value >> 24U) & 0xffU)
    };
}

void print_response(
    const std::string& label,
    const std::uint32_t request,
    const std::uint32_t response
) {
    const auto bytes = bytes_of(response);

    std::cout
        << label
        << " request=0x" << std::hex << std::uppercase
        << std::setw(8) << std::setfill('0') << request
        << " response=0x" << std::setw(8) << response
        << std::dec << std::setfill(' ')
        << " bytes=["
        << static_cast<unsigned int>(bytes[0]) << ","
        << static_cast<unsigned int>(bytes[1]) << ","
        << static_cast<unsigned int>(bytes[2]) << ","
        << static_cast<unsigned int>(bytes[3]) << "]"
        << " highByte=" << static_cast<unsigned int>(bytes[3])
        << '\n';
}

class ExactClient {
public:
    ExactClient() {
        HRESULT result = CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(locator_.GetAddressOf())
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "CoCreateInstance failed (" + hresult_text(result) + ")"
            );
        }

        ScopedBstr namespace_name(L"ROOT\\WMI");
        result = locator_->ConnectServer(
            namespace_name.get(),
            nullptr,
            nullptr,
            nullptr,
            WBEM_FLAG_CONNECT_USE_MAX_WAIT,
            nullptr,
            nullptr,
            services_.GetAddressOf()
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "ConnectServer failed (" + hresult_text(result) + ")"
            );
        }

        ScopedBstr class_name(L"AWCCWmiMethodFunction");
        result = services_->GetObject(
            class_name.get(),
            0,
            nullptr,
            class_definition_.GetAddressOf(),
            nullptr
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "GetObject failed (" + hresult_text(result) + ")"
            );
        }

        instance_path_ = find_instance_path();
    }

    [[nodiscard]] const std::wstring& instance_path() const noexcept {
        return instance_path_;
    }

    std::uint32_t call(
        const wchar_t* method_name,
        const std::uint8_t subcommand,
        const std::uint8_t argument1 = 0,
        const std::uint8_t argument2 = 0
    ) const {
        ComPtr<IWbemClassObject> input_parameters;
        HRESULT result = class_definition_->GetMethod(
            method_name,
            0,
            input_parameters.GetAddressOf(),
            nullptr
        );

        if (FAILED(result) || input_parameters == nullptr) {
            throw std::runtime_error(
                "GetMethod failed (" + hresult_text(result) + ")"
            );
        }

        const std::uint32_t request = pack_argument(
            subcommand,
            argument1,
            argument2
        );

        VARIANT parameters{VT_I4};
        parameters.uintVal = request;

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

        ScopedBstr object_path(instance_path_);
        ScopedBstr method(method_name);
        ComPtr<IWbemClassObject> output_parameters;

        result = services_->ExecMethod(
            object_path.get(),
            method.get(),
            0,
            nullptr,
            input_parameters.Get(),
            output_parameters.GetAddressOf(),
            nullptr
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "ExecMethod failed (" + hresult_text(result) + ")"
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

        const std::uint32_t response = output.uintVal;
        VariantClear(&output);
        return response;
    }

private:
    std::wstring find_instance_path() const {
        ScopedBstr class_name(L"AWCCWmiMethodFunction");
        ComPtr<IEnumWbemClassObject> enumerator;

        HRESULT result = services_->CreateInstanceEnum(
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

    ComPtr<IWbemLocator> locator_;
    ComPtr<IWbemServices> services_;
    ComPtr<IWbemClassObject> class_definition_;
    std::wstring instance_path_;
};

}  // namespace

int run_awcc_raw_probe() {
    ComRuntime runtime;
    const ExactClient client;

    std::wcout << L"AWCC instance: " << client.instance_path() << L"\n";
    std::cout
        << "All operations below are read-only. Values are shown as raw 32-bit "
        << "firmware responses.\n\n";

    const std::uint32_t system_request = pack_argument(0x02, 0x02, 0x00);
    const std::uint32_t system_response = client.call(
        L"Thermal_Information",
        0x02,
        0x02,
        0x00
    );
    print_response("system-id", system_request, system_response);

    std::cout << "\nresource enumeration:\n";
    int consecutive_zero_high_bytes = 0;

    for (std::uint8_t index = 0; index < 32; ++index) {
        const std::uint32_t request = pack_argument(0x03, index, 0x00);
        const std::uint32_t response = client.call(
            L"Thermal_Information",
            0x03,
            index,
            0x00
        );

        print_response(
            "  index " + std::to_string(index),
            request,
            response
        );

        const std::uint8_t high_byte = static_cast<std::uint8_t>(
            (response >> 24U) & 0xffU
        );

        if (high_byte == 0) {
            ++consecutive_zero_high_bytes;
            if (consecutive_zero_high_bytes >= 3) {
                break;
            }
        } else {
            consecutive_zero_high_bytes = 0;
        }
    }

    std::cout << "\ncurrent power profile:\n";
    const std::uint32_t power_request = pack_argument(0x0b, 0x00, 0x00);
    const std::uint32_t power_response = client.call(
        L"Thermal_Control",
        0x0b,
        0x00,
        0x00
    );
    print_response("  power", power_request, power_response);

    return 0;
}

}  // namespace awfan
