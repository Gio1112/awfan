#include "awfan/awcc_read.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace awfan {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kNamespace[] = L"ROOT\\WMI";
constexpr wchar_t kClassName[] = L"AWCCWmiMethodFunction";
constexpr wchar_t kThermalInformation[] = L"Thermal_Information";
constexpr wchar_t kThermalControl[] = L"Thermal_Control";
constexpr wchar_t kGetFanSensors[] = L"GetFanSensors";

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
            RPC_C_AUTHN_LEVEL_DEFAULT,
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

    ComRuntime(const ComRuntime&) = delete;
    ComRuntime& operator=(const ComRuntime&) = delete;

private:
    HRESULT result_{E_FAIL};
};

std::string hresult_text(const HRESULT result) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return output.str();
}

std::string narrow_ascii(const std::wstring& value) {
    std::string output;
    output.reserve(value.size());

    for (const wchar_t character : value) {
        output.push_back(
            character >= 0 && character <= 0x7f
                ? static_cast<char>(character)
                : '?'
        );
    }

    return output;
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;

    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    output << "\\u"
                           << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(character)
                           << std::dec;
                } else {
                    output << static_cast<char>(character);
                }
                break;
        }
    }

    return output.str();
}

std::int64_t integer_from_variant(const VARIANT& value) {
    switch (value.vt) {
        case VT_I1: return value.cVal;
        case VT_UI1: return value.bVal;
        case VT_I2: return value.iVal;
        case VT_UI2: return value.uiVal;
        case VT_I4:
        case VT_INT: return value.lVal;
        case VT_UI4:
        case VT_UINT: return value.ulVal;
        case VT_I8: return value.llVal;
        case VT_UI8:
            if (value.ullVal > static_cast<ULONGLONG>(
                    std::numeric_limits<std::int64_t>::max()
                )) {
                throw std::runtime_error("WMI result exceeded signed 64-bit range");
            }
            return static_cast<std::int64_t>(value.ullVal);
        default:
            throw std::runtime_error("WMI returned a non-integer result");
    }
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

template <typename T>
void append_unique(std::vector<T>& values, const T value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

struct FanReading {
    std::uint8_t id{0};
    std::int64_t sensor_id{-1};
    std::int64_t rpm{-1};
    std::int64_t maximum_rpm{-1};
    std::int64_t percent{-1};
    std::int64_t boost{-1};
};

struct SensorReading {
    std::uint8_t id{0};
    std::int64_t temperature{-1};
};

struct StatusSnapshot {
    std::string instance_path;
    std::int64_t system_id{-1};
    std::int64_t current_power{-1};
    std::vector<std::uint8_t> power_profiles;
    std::vector<FanReading> fans;
    std::vector<SensorReading> sensors;
};

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
                "Could not create IWbemLocator (" + hresult_text(result) + ")"
            );
        }

        ScopedBstr namespace_name(kNamespace);
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
                "Could not connect to ROOT\\WMI (" + hresult_text(result) + ")"
            );
        }

        result = CoSetProxyBlanket(
            services_.Get(),
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "CoSetProxyBlanket failed (" + hresult_text(result) + ")"
            );
        }

        ScopedBstr class_name(kClassName);
        result = services_->GetObject(
            class_name.get(),
            0,
            nullptr,
            class_definition_.GetAddressOf(),
            nullptr
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "AWCCWmiMethodFunction was not found (" +
                hresult_text(result) + ")"
            );
        }

        instance_path_ = find_active_instance();
    }

    [[nodiscard]] const std::wstring& instance_path() const noexcept {
        return instance_path_;
    }

    std::int64_t call(
        const wchar_t* method_name,
        const std::uint8_t subcommand,
        const std::uint8_t argument1 = 0,
        const std::uint8_t argument2 = 0
    ) const {
        ComPtr<IWbemClassObject> input_definition;
        HRESULT result = class_definition_->GetMethod(
            method_name,
            0,
            input_definition.GetAddressOf(),
            nullptr
        );

        if (FAILED(result) || input_definition == nullptr) {
            throw std::runtime_error(
                "Input signature unavailable for " +
                narrow_ascii(method_name) + " (" + hresult_text(result) + ")"
            );
        }

        ComPtr<IWbemClassObject> input_parameters;
        result = input_definition->SpawnInstance(
            0,
            input_parameters.GetAddressOf()
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "Could not create input parameters for " +
                narrow_ascii(method_name) + " (" + hresult_text(result) + ")"
            );
        }

        // WMI reports this MOF field as uint32, but WmiProv expects the
        // automation representation used by AlienFX Tools: VT_I4 with the
        // packed unsigned bits stored in uintVal, and no CIM type override.
        VARIANT argument;
        VariantInit(&argument);
        argument.vt = VT_I4;
        argument.uintVal = pack_argument(subcommand, argument1, argument2);

        result = input_parameters->Put(
            L"arg2",
            0,
            &argument,
            0
        );
        VariantClear(&argument);

        if (FAILED(result)) {
            throw std::runtime_error(
                "Could not set arg2 for " + narrow_ascii(method_name) +
                " (" + hresult_text(result) + ")"
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
            std::string message = "ExecMethod failed for " +
                narrow_ascii(method_name) + " (" + hresult_text(result) + ")";

            if (result == E_ACCESSDENIED || result == WBEM_E_ACCESS_DENIED) {
                message += ". Run this test from an Administrator terminal.";
            }

            throw std::runtime_error(message);
        }

        if (output_parameters == nullptr) {
            throw std::runtime_error(
                "No output parameters returned for " + narrow_ascii(method_name)
            );
        }

        VARIANT output;
        VariantInit(&output);
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
                "No argr result returned for " + narrow_ascii(method_name) +
                " (" + hresult_text(result) + ")"
            );
        }

        const std::int64_t value = integer_from_variant(output);
        VariantClear(&output);
        return value;
    }

private:
    std::wstring find_active_instance() const {
        ScopedBstr class_name(kClassName);
        ComPtr<IEnumWbemClassObject> enumerator;

        HRESULT result = services_->CreateInstanceEnum(
            class_name.get(),
            WBEM_FLAG_FORWARD_ONLY,
            nullptr,
            enumerator.GetAddressOf()
        );

        if (FAILED(result)) {
            throw std::runtime_error(
                "Could not enumerate AWCC instances (" +
                hresult_text(result) + ")"
            );
        }

        while (true) {
            ComPtr<IWbemClassObject> instance;
            ULONG returned = 0;

            result = enumerator->Next(
                10000,
                1,
                instance.GetAddressOf(),
                &returned
            );

            if (result == WBEM_S_FALSE || returned == 0) {
                break;
            }

            if (FAILED(result)) {
                throw std::runtime_error(
                    "AWCC instance enumeration failed (" +
                    hresult_text(result) + ")"
                );
            }

            VARIANT active;
            VariantInit(&active);
            const HRESULT active_result = instance->Get(
                L"Active",
                0,
                &active,
                nullptr,
                nullptr
            );

            const bool active_instance = FAILED(active_result)
                || active.vt != VT_BOOL
                || active.boolVal == VARIANT_TRUE;
            VariantClear(&active);

            if (!active_instance) {
                continue;
            }

            VARIANT path;
            VariantInit(&path);
            result = instance->Get(
                L"__PATH",
                0,
                &path,
                nullptr,
                nullptr
            );

            if (SUCCEEDED(result) && path.vt == VT_BSTR && path.bstrVal != nullptr) {
                const std::wstring value = path.bstrVal;
                VariantClear(&path);
                return value;
            }

            VariantClear(&path);
        }

        throw std::runtime_error("No active AWCCWmiMethodFunction instance found");
    }

    ComPtr<IWbemLocator> locator_;
    ComPtr<IWbemServices> services_;
    ComPtr<IWbemClassObject> class_definition_;
    std::wstring instance_path_;
};

StatusSnapshot collect_status(const AwccClient& client) {
    StatusSnapshot snapshot;
    snapshot.instance_path = narrow_ascii(client.instance_path());
    snapshot.system_id = client.call(kThermalInformation, 0x02, 0x02);

    std::vector<std::uint8_t> fan_ids;
    std::vector<std::uint8_t> sensor_ids;

    for (std::uint16_t index = 0; index < 64; ++index) {
        const std::int64_t raw = client.call(
            kThermalInformation,
            0x03,
            static_cast<std::uint8_t>(index)
        );

        if (raw <= 0) {
            break;
        }

        const auto raw32 = static_cast<std::uint32_t>(raw);
        const auto identifier = static_cast<std::uint8_t>(raw32 & 0xffU);

        if (raw32 > 0x100U && raw32 < 0x110U) {
            append_unique(sensor_ids, identifier);
        } else if (raw32 > 0x8fU) {
            append_unique(snapshot.power_profiles, identifier);
        } else {
            append_unique(fan_ids, identifier);
        }
    }

    snapshot.current_power = client.call(kThermalControl, 0x0b);

    for (const std::uint8_t fan_id : fan_ids) {
        FanReading fan;
        fan.id = fan_id;
        fan.sensor_id = client.call(kGetFanSensors, 0x02, fan_id);
        fan.rpm = client.call(kThermalInformation, 0x05, fan_id);
        fan.maximum_rpm = client.call(kThermalInformation, 0x09, fan_id);
        fan.percent = client.call(kThermalInformation, 0x06, fan_id);
        fan.boost = client.call(kThermalInformation, 0x0c, fan_id);
        snapshot.fans.push_back(fan);
    }

    for (const std::uint8_t sensor_id : sensor_ids) {
        SensorReading sensor;
        sensor.id = sensor_id;
        sensor.temperature = client.call(kThermalInformation, 0x04, sensor_id);
        snapshot.sensors.push_back(sensor);
    }

    return snapshot;
}

std::string sensor_name(const std::size_t index) {
    switch (index) {
        case 0: return "CPU Internal Thermistor";
        case 1: return "GPU Internal Thermistor";
        case 2: return "Motherboard Thermistor";
        default: return "Sensor " + std::to_string(index);
    }
}

void print_human(const StatusSnapshot& snapshot) {
    std::cout
        << "awfan native read-only status\n"
        << "Instance: " << snapshot.instance_path << '\n'
        << "System ID: " << snapshot.system_id << " (0x"
        << std::hex << std::uppercase << snapshot.system_id
        << std::dec << ")\n"
        << "Current power profile: " << snapshot.current_power << " (0x"
        << std::hex << std::uppercase << snapshot.current_power
        << std::dec << ")\n";

    std::cout << "Available power profiles:";
    if (snapshot.power_profiles.empty()) {
        std::cout << " none";
    } else {
        for (const std::uint8_t profile : snapshot.power_profiles) {
            std::cout << " 0x" << std::hex << std::uppercase
                      << static_cast<unsigned int>(profile) << std::dec;
        }
    }

    std::cout << "\n\nFans:\n";
    if (snapshot.fans.empty()) {
        std::cout << "  none discovered\n";
    }

    for (std::size_t index = 0; index < snapshot.fans.size(); ++index) {
        const FanReading& fan = snapshot.fans[index];
        std::cout
            << "  Fan " << index
            << " (firmware ID 0x" << std::hex << std::uppercase
            << static_cast<unsigned int>(fan.id) << std::dec << ")\n"
            << "    Sensor ID: " << fan.sensor_id << '\n'
            << "    RPM: " << fan.rpm << '\n'
            << "    Maximum RPM: " << fan.maximum_rpm << '\n'
            << "    Percent: " << fan.percent << "%\n"
            << "    Boost: " << fan.boost << "%\n";
    }

    std::cout << "\nTemperatures:\n";
    if (snapshot.sensors.empty()) {
        std::cout << "  none discovered\n";
    }

    for (std::size_t index = 0; index < snapshot.sensors.size(); ++index) {
        const SensorReading& sensor = snapshot.sensors[index];
        std::cout
            << "  " << sensor_name(index)
            << " (firmware ID 0x" << std::hex << std::uppercase
            << static_cast<unsigned int>(sensor.id) << std::dec << "): ";

        if (sensor.temperature < 0 || sensor.temperature > 150) {
            std::cout << "invalid raw value " << sensor.temperature << '\n';
        } else {
            std::cout << sensor.temperature << " C\n";
        }
    }
}

void print_json(const StatusSnapshot& snapshot) {
    std::cout
        << "{\n"
        << "  \"mode\": \"read-only\",\n"
        << "  \"instancePath\": \""
        << json_escape(snapshot.instance_path) << "\",\n"
        << "  \"systemId\": " << snapshot.system_id << ",\n"
        << "  \"currentPowerProfile\": " << snapshot.current_power << ",\n"
        << "  \"powerProfiles\": [";

    for (std::size_t index = 0; index < snapshot.power_profiles.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << static_cast<unsigned int>(snapshot.power_profiles[index]);
    }

    std::cout << "],\n  \"fans\": [";

    for (std::size_t index = 0; index < snapshot.fans.size(); ++index) {
        const FanReading& fan = snapshot.fans[index];
        std::cout
            << (index == 0 ? "\n" : ",\n")
            << "    {\"index\": " << index
            << ", \"id\": " << static_cast<unsigned int>(fan.id)
            << ", \"sensorId\": " << fan.sensor_id
            << ", \"rpm\": " << fan.rpm
            << ", \"maxRpm\": " << fan.maximum_rpm
            << ", \"percent\": " << fan.percent
            << ", \"boost\": " << fan.boost << "}";
    }

    if (!snapshot.fans.empty()) {
        std::cout << '\n';
    }

    std::cout << "  ],\n  \"sensors\": [";

    for (std::size_t index = 0; index < snapshot.sensors.size(); ++index) {
        const SensorReading& sensor = snapshot.sensors[index];
        std::cout
            << (index == 0 ? "\n" : ",\n")
            << "    {\"index\": " << index
            << ", \"id\": " << static_cast<unsigned int>(sensor.id)
            << ", \"name\": \"" << json_escape(sensor_name(index))
            << "\", \"temperature\": " << sensor.temperature << "}";
    }

    if (!snapshot.sensors.empty()) {
        std::cout << '\n';
    }

    std::cout << "  ]\n}\n";
}

}  // namespace

int run_awcc_read_status(const bool json_output) {
    ComRuntime runtime;
    const AwccClient client;
    const StatusSnapshot snapshot = collect_status(client);

    if (json_output) {
        print_json(snapshot);
    } else {
        print_human(snapshot);
    }

    return 0;
}

}  // namespace awfan
