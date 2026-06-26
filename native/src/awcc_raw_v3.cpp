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

std::string hresult_text(const HRESULT result) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return stream.str();
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

struct CallResult {
    HRESULT hresult{S_OK};
    std::uint32_t value{0xffffffffU};

    [[nodiscard]] bool succeeded() const noexcept {
        return SUCCEEDED(hresult);
    }
};

void print_response(
    const std::string& label,
    const std::uint32_t request,
    const CallResult& result
) {
    std::cout
        << label
        << " request=0x" << std::hex << std::uppercase
        << std::setw(8) << std::setfill('0') << request;

    if (!result.succeeded()) {
        std::cout
            << " provider-result=" << hresult_text(result.hresult)
            << " (treated as -1 terminator)\n";
        return;
    }

    const auto bytes = bytes_of(result.value);
    std::cout
        << " response=0x" << std::setw(8) << result.value
        << std::dec << std::setfill(' ')
        << " signed=" << static_cast<std::int32_t>(result.value)
        << " bytes=["
        << static_cast<unsigned int>(bytes[0]) << ","
        << static_cast<unsigned int>(bytes[1]) << ","
        << static_cast<unsigned int>(bytes[2]) << ","
        << static_cast<unsigned int>(bytes[3]) << "]\n";
}

class AwccClient {
public:
    AwccClient() {
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

        result = class_definition_->GetMethod(
            L"Thermal_Information",
            0,
            shared_input_parameters_.GetAddressOf(),
            nullptr
        );
        if (FAILED(result) || shared_input_parameters_ == nullptr) {
            throw std::runtime_error(
                "Could not obtain the AWCC input object (" +
                hresult_text(result) + ")"
            );
        }
    }

    [[nodiscard]] const std::wstring& instance_path() const noexcept {
        return instance_path_;
    }

    CallResult call(
        const wchar_t* method_name,
        const std::uint8_t subcommand,
        const std::uint8_t argument1 = 0,
        const std::uint8_t argument2 = 0
    ) const {
        VARIANT parameters{VT_I4};
        parameters.uintVal = pack_argument(subcommand, argument1, argument2);

        HRESULT result = shared_input_parameters_->Put(
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
            shared_input_parameters_.Get(),
            output_parameters.GetAddressOf(),
            nullptr
        );

        if (FAILED(result) || output_parameters == nullptr) {
            return {result, 0xffffffffU};
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
            return {result, 0xffffffffU};
        }

        const std::uint32_t value = output.uintVal;
        VariantClear(&output);
        return {S_OK, value};
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
    ComPtr<IWbemClassObject> shared_input_parameters_;
    std::wstring instance_path_;
};

CallResult print_read(
    const AwccClient& client,
    const std::string& label,
    const wchar_t* method,
    const std::uint8_t subcommand,
    const std::uint8_t argument1 = 0,
    const std::uint8_t argument2 = 0
) {
    const std::uint32_t request = pack_argument(
        subcommand,
        argument1,
        argument2
    );
    const CallResult result = client.call(
        method,
        subcommand,
        argument1,
        argument2
    );
    print_response(label, request, result);
    return result;
}

std::string hex_id(const std::uint8_t id) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << static_cast<unsigned int>(id);
    return stream.str();
}

}  // namespace

int run_awcc_raw_probe() {
    ComRuntime runtime;
    const AwccClient client;

    std::wcout << L"AWCC instance: " << client.instance_path() << L"\n";
    std::cout
        << "All operations below are read-only. Provider failures are shown "
        << "as -1-style results instead of aborting the diagnostic.\n\n";

    print_read(
        client,
        "system-id",
        L"Thermal_Information",
        0x02,
        0x02
    );

    std::vector<std::uint8_t> fan_ids;
    std::vector<std::uint8_t> sensor_ids;
    std::vector<std::uint8_t> power_ids;

    std::cout << "\nresource enumeration:\n";
    for (std::uint8_t index = 0; index < 64; ++index) {
        const CallResult result = print_read(
            client,
            "  index " + std::to_string(index),
            L"Thermal_Information",
            0x03,
            index
        );

        if (!result.succeeded()) {
            break;
        }

        const std::int32_t signed_value = static_cast<std::int32_t>(
            result.value
        );
        if (signed_value <= 0) {
            break;
        }

        const std::uint8_t id = static_cast<std::uint8_t>(
            result.value & 0xffU
        );

        if (result.value > 0x100U && result.value < 0x110U) {
            sensor_ids.push_back(id);
        } else if (result.value > 0x8fU) {
            power_ids.push_back(id);
        } else {
            fan_ids.push_back(id);
        }
    }

    std::cout << "\ncurrent power profile:\n";
    print_read(
        client,
        "  power",
        L"Thermal_Information",
        0x0b
    );

    std::cout << "\nfan reads:\n";
    for (const std::uint8_t fan_id : fan_ids) {
        const std::string prefix = "  fan 0x" + hex_id(fan_id);

        print_read(
            client,
            prefix + " sensor",
            L"GetFanSensors",
            0x02,
            fan_id
        );
        print_read(
            client,
            prefix + " rpm",
            L"Thermal_Information",
            0x05,
            fan_id
        );
        print_read(
            client,
            prefix + " max-rpm",
            L"Thermal_Information",
            0x09,
            fan_id
        );
        print_read(
            client,
            prefix + " percent",
            L"Thermal_Information",
            0x06,
            fan_id
        );
        print_read(
            client,
            prefix + " boost",
            L"Thermal_Information",
            0x0c,
            fan_id
        );
    }

    std::cout << "\ntemperature reads:\n";
    for (const std::uint8_t sensor_id : sensor_ids) {
        print_read(
            client,
            "  sensor 0x" + hex_id(sensor_id),
            L"Thermal_Information",
            0x04,
            sensor_id
        );
    }

    std::cout << "\ndiscovered power profile IDs:";
    if (power_ids.empty()) {
        std::cout << " none";
    } else {
        for (const std::uint8_t id : power_ids) {
            std::cout
                << " 0x" << std::hex << std::uppercase
                << static_cast<unsigned int>(id) << std::dec;
        }
    }
    std::cout << '\n';

    return 0;
}

}  // namespace awfan
