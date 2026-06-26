#include "awfan/native_cli.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace awfan {
namespace {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

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

std::string hresult_text(const HRESULT result) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return stream.str();
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

std::string hex32(const std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase
           << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

std::string hex8(const std::uint8_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase
           << std::setw(2) << std::setfill('0')
           << static_cast<unsigned int>(value);
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

    ComRuntime(const ComRuntime&) = delete;
    ComRuntime& operator=(const ComRuntime&) = delete;

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

struct CallResult {
    HRESULT hresult{S_OK};
    std::uint32_t value{0xffffffffU};

    [[nodiscard]] bool succeeded() const noexcept {
        return SUCCEEDED(hresult);
    }

    [[nodiscard]] std::int32_t signed_value() const noexcept {
        return static_cast<std::int32_t>(value);
    }
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

        instance_path_ = find_instance_path();

        result = class_definition_->GetMethod(
            kThermalInformation,
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

        discover_resources();
    }

    [[nodiscard]] const std::wstring& instance_path() const noexcept {
        return instance_path_;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& fan_ids() const noexcept {
        return fan_ids_;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& sensor_ids() const noexcept {
        return sensor_ids_;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& power_profiles() const noexcept {
        return power_profiles_;
    }

    CallResult call(
        const wchar_t* method_name,
        const std::uint8_t subcommand,
        const std::uint8_t argument1 = 0,
        const std::uint8_t argument2 = 0
    ) const {
        VARIANT parameters{VT_I4};
        parameters.uintVal = pack_argument(
            subcommand,
            argument1,
            argument2
        );

        HRESULT result = shared_input_parameters_->Put(
            L"arg2",
            0,
            &parameters,
            0
        );
        if (FAILED(result)) {
            throw std::runtime_error(
                "Could not set arg2 (" + hresult_text(result) + ")"
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

    CallResult set_power_raw(const std::uint8_t profile) const {
        return call(kThermalControl, 0x01, profile, 0x00);
    }

    CallResult set_fan_boost(
        const std::uint8_t fan_id,
        const std::uint8_t percent
    ) const {
        return call(kThermalControl, 0x02, fan_id, percent);
    }

private:
    std::wstring find_instance_path() const {
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
                "No active AWCC instance was returned (" +
                hresult_text(result) + ")"
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
                "Could not read the AWCC instance path (" +
                hresult_text(result) + ")"
            );
        }

        const std::wstring value = path.bstrVal;
        VariantClear(&path);
        return value;
    }

    void discover_resources() {
        power_profiles_.push_back(0x00);

        for (std::uint8_t index = 0; index < 64; ++index) {
            const CallResult result = call(
                kThermalInformation,
                0x03,
                index,
                0x00
            );

            if (!result.succeeded() || result.signed_value() <= 0) {
                break;
            }

            const std::uint8_t id = static_cast<std::uint8_t>(
                result.value & 0xffU
            );

            if (result.value > 0x100U && result.value < 0x110U) {
                sensor_ids_.push_back(id);
            } else if (result.value > 0x8fU) {
                if (std::find(
                        power_profiles_.begin(),
                        power_profiles_.end(),
                        id
                    ) == power_profiles_.end()) {
                    power_profiles_.push_back(id);
                }
            } else {
                fan_ids_.push_back(id);
            }
        }
    }

    ComPtr<IWbemLocator> locator_;
    ComPtr<IWbemServices> services_;
    ComPtr<IWbemClassObject> class_definition_;
    ComPtr<IWbemClassObject> shared_input_parameters_;
    std::wstring instance_path_;
    std::vector<std::uint8_t> fan_ids_;
    std::vector<std::uint8_t> sensor_ids_;
    std::vector<std::uint8_t> power_profiles_;
};

enum class TargetMode {
    unknown,
    manual,
    firmware
};

struct TargetState {
    TargetMode mode{TargetMode::unknown};
    std::optional<int> cpu_percent;
    std::optional<int> gpu_percent;
    std::optional<int> profile_index;
    std::optional<int> raw_profile;
    std::optional<std::int64_t> updated_epoch;
};

fs::path target_state_path() {
    const wchar_t* local_app_data = _wgetenv(L"LOCALAPPDATA");
    if (local_app_data == nullptr || *local_app_data == L'\0') {
        throw std::runtime_error("LOCALAPPDATA is unavailable.");
    }

    return fs::path(local_app_data) / L"awfan" / L"native-target.state";
}

std::optional<int> parse_optional_int(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 10);
        if (consumed == value.size()) {
            return parsed;
        }
    } catch (...) {
    }

    return std::nullopt;
}

TargetState load_target_state() {
    TargetState state;
    const fs::path path = target_state_path();

    std::ifstream input(path);
    if (!input) {
        return state;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);

        if (key == "mode") {
            if (value == "manual") {
                state.mode = TargetMode::manual;
            } else if (value == "firmware") {
                state.mode = TargetMode::firmware;
            }
        } else if (key == "cpu") {
            state.cpu_percent = parse_optional_int(value);
        } else if (key == "gpu") {
            state.gpu_percent = parse_optional_int(value);
        } else if (key == "profile") {
            state.profile_index = parse_optional_int(value);
        } else if (key == "raw") {
            state.raw_profile = parse_optional_int(value);
        } else if (key == "updated") {
            try {
                state.updated_epoch = std::stoll(value);
            } catch (...) {
            }
        }
    }

    return state;
}

void save_target_state(const TargetState& state) {
    const fs::path path = target_state_path();
    fs::create_directories(path.parent_path());

    const fs::path temporary = path.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not write native target state.");
    }

    const char* mode = "unknown";
    if (state.mode == TargetMode::manual) {
        mode = "manual";
    } else if (state.mode == TargetMode::firmware) {
        mode = "firmware";
    }

    output << "mode=" << mode << '\n';
    output << "cpu=";
    if (state.cpu_percent.has_value()) {
        output << *state.cpu_percent;
    }
    output << '\n';
    output << "gpu=";
    if (state.gpu_percent.has_value()) {
        output << *state.gpu_percent;
    }
    output << '\n';
    output << "profile=";
    if (state.profile_index.has_value()) {
        output << *state.profile_index;
    }
    output << '\n';
    output << "raw=";
    if (state.raw_profile.has_value()) {
        output << *state.raw_profile;
    }
    output << '\n';
    output << "updated=";
    if (state.updated_epoch.has_value()) {
        output << *state.updated_epoch;
    }
    output << '\n';
    output.close();

    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(path, error);
        error.clear();
        fs::rename(temporary, path, error);
    }

    if (error) {
        throw std::runtime_error("Could not replace native target state.");
    }
}

std::int64_t current_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

struct FanReading {
    std::uint8_t id{0};
    std::optional<std::uint8_t> sensor_id;
    std::optional<std::int32_t> rpm;
    std::optional<std::int32_t> maximum_rpm;
    std::optional<std::int32_t> reported_percent;
    std::optional<double> calculated_percent;
    std::optional<std::int32_t> boost;
    std::optional<int> requested_percent;
    std::optional<int> estimated_target_rpm;
    std::optional<int> rpm_delta;
    std::optional<std::string> transition;
};

struct SensorReading {
    std::uint8_t id{0};
    std::optional<std::int32_t> temperature;
};

struct Snapshot {
    std::string instance_path;
    std::optional<std::uint32_t> system_signature;
    std::optional<std::int32_t> current_power;
    std::vector<std::uint8_t> power_profiles;
    std::vector<FanReading> fans;
    std::vector<SensorReading> sensors;
    TargetState target_state;
};

std::optional<std::int32_t> nonnegative_value(const CallResult& result) {
    if (!result.succeeded() || result.signed_value() < 0) {
        return std::nullopt;
    }
    return result.signed_value();
}

std::string sensor_name(const std::size_t index) {
    switch (index) {
        case 0: return "CPU Internal Thermistor";
        case 1: return "GPU Internal Thermistor";
        case 2: return "Motherboard Thermistor";
        default: return "Sensor " + std::to_string(index);
    }
}

std::string target_mode_name(const TargetMode mode) {
    switch (mode) {
        case TargetMode::manual: return "manual";
        case TargetMode::firmware: return "firmware";
        default: return "unknown";
    }
}

void apply_target_state(Snapshot& snapshot) {
    if (snapshot.target_state.mode != TargetMode::manual) {
        return;
    }

    for (std::size_t index = 0; index < snapshot.fans.size(); ++index) {
        FanReading& fan = snapshot.fans[index];
        const std::optional<int> requested = index == 0
            ? snapshot.target_state.cpu_percent
            : index == 1
                ? snapshot.target_state.gpu_percent
                : std::nullopt;

        if (!requested.has_value()) {
            continue;
        }

        fan.requested_percent = requested;

        if (!fan.maximum_rpm.has_value() || *fan.maximum_rpm <= 0) {
            continue;
        }

        fan.estimated_target_rpm = static_cast<int>(std::lround(
            static_cast<double>(*fan.maximum_rpm)
            * static_cast<double>(*requested)
            / 100.0
        ));

        if (!fan.rpm.has_value()) {
            continue;
        }

        fan.rpm_delta = *fan.rpm - *fan.estimated_target_rpm;
        const int tolerance = std::max(
            100,
            static_cast<int>(std::lround(
                static_cast<double>(*fan.maximum_rpm) * 0.02
            ))
        );

        if (std::abs(*fan.rpm_delta) <= tolerance) {
            fan.transition = "near target";
        } else if (*fan.rpm_delta > 0) {
            fan.transition = "above target; expected to slow down";
        } else {
            fan.transition = "below target; expected to speed up";
        }
    }
}

Snapshot collect_snapshot(const AwccClient& client) {
    Snapshot snapshot;
    snapshot.instance_path = narrow_ascii(client.instance_path());
    snapshot.power_profiles = client.power_profiles();
    snapshot.target_state = load_target_state();

    const CallResult signature = client.call(
        kThermalInformation,
        0x02,
        0x02
    );
    if (signature.succeeded()) {
        snapshot.system_signature = signature.value;
    }

    snapshot.current_power = nonnegative_value(
        client.call(kThermalInformation, 0x0b)
    );

    for (const std::uint8_t fan_id : client.fan_ids()) {
        FanReading fan;
        fan.id = fan_id;

        const auto mapped_sensor = nonnegative_value(
            client.call(kGetFanSensors, 0x02, fan_id)
        );
        if (mapped_sensor.has_value() && *mapped_sensor <= 0xff) {
            fan.sensor_id = static_cast<std::uint8_t>(*mapped_sensor);
        }

        fan.rpm = nonnegative_value(
            client.call(kThermalInformation, 0x05, fan_id)
        );
        fan.maximum_rpm = nonnegative_value(
            client.call(kThermalInformation, 0x09, fan_id)
        );
        fan.reported_percent = nonnegative_value(
            client.call(kThermalInformation, 0x06, fan_id)
        );
        fan.boost = nonnegative_value(
            client.call(kThermalInformation, 0x0c, fan_id)
        );

        if (fan.rpm.has_value() && fan.maximum_rpm.has_value()
            && *fan.maximum_rpm > 0) {
            fan.calculated_percent =
                static_cast<double>(*fan.rpm) * 100.0
                / static_cast<double>(*fan.maximum_rpm);
        }

        snapshot.fans.push_back(fan);
    }

    for (const std::uint8_t sensor_id : client.sensor_ids()) {
        SensorReading sensor;
        sensor.id = sensor_id;
        sensor.temperature = nonnegative_value(
            client.call(kThermalInformation, 0x04, sensor_id)
        );

        if (sensor.temperature.has_value() && *sensor.temperature > 150) {
            sensor.temperature = std::nullopt;
        }

        snapshot.sensors.push_back(sensor);
    }

    apply_target_state(snapshot);
    return snapshot;
}

std::string profile_label(
    const Snapshot& snapshot,
    const std::int32_t raw_profile
) {
    if (raw_profile == 0) {
        return "Manual (0x00)";
    }

    const auto found = std::find(
        snapshot.power_profiles.begin(),
        snapshot.power_profiles.end(),
        static_cast<std::uint8_t>(raw_profile)
    );

    if (found != snapshot.power_profiles.end()) {
        const auto index = static_cast<std::size_t>(
            std::distance(snapshot.power_profiles.begin(), found)
        );
        return "Firmware profile " + std::to_string(index) + " (" +
            hex8(static_cast<std::uint8_t>(raw_profile)) + ")";
    }

    return "Unknown (" + hex8(static_cast<std::uint8_t>(raw_profile)) + ")";
}

void print_optional_integer(
    const std::optional<std::int32_t>& value,
    const std::string& suffix = ""
) {
    if (value.has_value()) {
        std::cout << *value << suffix;
    } else {
        std::cout << "unavailable";
    }
}

void print_fans_human(const Snapshot& snapshot) {
    if (snapshot.fans.empty()) {
        std::cout << "No fans discovered.\n";
        return;
    }

    for (std::size_t index = 0; index < snapshot.fans.size(); ++index) {
        const FanReading& fan = snapshot.fans[index];
        std::cout
            << "Fan " << index << " (firmware ID " << hex8(fan.id) << ")\n"
            << "  Sensor: ";

        if (fan.sensor_id.has_value()) {
            std::cout << hex8(*fan.sensor_id);
        } else {
            std::cout << "unavailable";
        }

        std::cout << "\n  RPM: ";
        print_optional_integer(fan.rpm);
        std::cout << "\n  Maximum RPM: ";
        print_optional_integer(fan.maximum_rpm);
        std::cout << "\n  Calculated percent: ";

        if (fan.calculated_percent.has_value()) {
            std::cout << std::fixed << std::setprecision(1)
                      << *fan.calculated_percent << "%"
                      << std::defaultfloat;
        } else {
            std::cout << "unavailable";
        }

        std::cout << "\n  Firmware boost: ";
        print_optional_integer(fan.boost, "%");

        if (fan.requested_percent.has_value()) {
            std::cout
                << "\n  Requested target: " << *fan.requested_percent << "%";

            if (fan.estimated_target_rpm.has_value()) {
                std::cout
                    << " (~" << *fan.estimated_target_rpm
                    << " RPM estimated)";
            }

            if (fan.transition.has_value()) {
                std::cout << "\n  Transition: " << *fan.transition;

                if (fan.rpm_delta.has_value()) {
                    std::cout
                        << " ("
                        << (*fan.rpm_delta >= 0 ? "+" : "")
                        << *fan.rpm_delta << " RPM from target)";
                }
            }
        } else if (snapshot.target_state.mode == TargetMode::firmware) {
            std::cout << "\n  Target: firmware-controlled and dynamic";
        } else {
            std::cout << "\n  Target: unknown; no native manual target has been saved";
        }

        std::cout << "\n";

        if (index + 1 < snapshot.fans.size()) {
            std::cout << '\n';
        }
    }
}

void print_temps_human(const Snapshot& snapshot) {
    if (snapshot.sensors.empty()) {
        std::cout << "No firmware temperature sensors discovered.\n";
        return;
    }

    for (std::size_t index = 0; index < snapshot.sensors.size(); ++index) {
        const SensorReading& sensor = snapshot.sensors[index];
        std::cout
            << sensor_name(index)
            << " (firmware ID " << hex8(sensor.id) << "): ";
        print_optional_integer(sensor.temperature, " C");
        std::cout << '\n';
    }
}

void print_profiles_human(const Snapshot& snapshot) {
    std::cout << "Current: ";
    if (snapshot.current_power.has_value()) {
        std::cout << profile_label(snapshot, *snapshot.current_power);
    } else {
        std::cout << "unavailable";
    }

    std::cout
        << "\nTarget source: "
        << target_mode_name(snapshot.target_state.mode)
        << "\n\nAvailable profiles:\n";

    for (std::size_t index = 0; index < snapshot.power_profiles.size(); ++index) {
        const std::uint8_t profile = snapshot.power_profiles[index];
        if (index == 0) {
            std::cout << "  0  Manual (0x00)\n";
        } else {
            std::cout
                << "  " << index << "  Firmware profile " << index
                << " (" << hex8(profile) << ")\n";
        }
    }
}

void print_json_optional_integer(
    const std::optional<std::int32_t>& value
) {
    if (value.has_value()) {
        std::cout << *value;
    } else {
        std::cout << "null";
    }
}

void print_json_optional_plain_int(const std::optional<int>& value) {
    if (value.has_value()) {
        std::cout << *value;
    } else {
        std::cout << "null";
    }
}

void print_fans_json(const Snapshot& snapshot) {
    std::cout << "[";

    for (std::size_t index = 0; index < snapshot.fans.size(); ++index) {
        const FanReading& fan = snapshot.fans[index];
        std::cout
            << (index == 0 ? "\n" : ",\n")
            << "  {\"index\": " << index
            << ", \"id\": " << static_cast<unsigned int>(fan.id)
            << ", \"sensorId\": ";

        if (fan.sensor_id.has_value()) {
            std::cout << static_cast<unsigned int>(*fan.sensor_id);
        } else {
            std::cout << "null";
        }

        std::cout << ", \"rpm\": ";
        print_json_optional_integer(fan.rpm);
        std::cout << ", \"maxRpm\": ";
        print_json_optional_integer(fan.maximum_rpm);
        std::cout << ", \"reportedPercent\": ";
        print_json_optional_integer(fan.reported_percent);
        std::cout << ", \"calculatedPercent\": ";

        if (fan.calculated_percent.has_value()) {
            std::cout << std::fixed << std::setprecision(2)
                      << *fan.calculated_percent << std::defaultfloat;
        } else {
            std::cout << "null";
        }

        std::cout << ", \"boost\": ";
        print_json_optional_integer(fan.boost);
        std::cout << ", \"requestedPercent\": ";
        print_json_optional_plain_int(fan.requested_percent);
        std::cout << ", \"estimatedTargetRpm\": ";
        print_json_optional_plain_int(fan.estimated_target_rpm);
        std::cout << ", \"rpmDelta\": ";
        print_json_optional_plain_int(fan.rpm_delta);
        std::cout << ", \"transition\": ";

        if (fan.transition.has_value()) {
            std::cout << "\"" << json_escape(*fan.transition) << "\"";
        } else {
            std::cout << "null";
        }

        std::cout << '}';
    }

    if (!snapshot.fans.empty()) {
        std::cout << '\n';
    }

    std::cout << "]";
}

void print_temps_json(const Snapshot& snapshot) {
    std::cout << "[";

    for (std::size_t index = 0; index < snapshot.sensors.size(); ++index) {
        const SensorReading& sensor = snapshot.sensors[index];
        std::cout
            << (index == 0 ? "\n" : ",\n")
            << "  {\"index\": " << index
            << ", \"id\": " << static_cast<unsigned int>(sensor.id)
            << ", \"name\": \"" << json_escape(sensor_name(index))
            << "\", \"temperature\": ";
        print_json_optional_integer(sensor.temperature);
        std::cout << '}';
    }

    if (!snapshot.sensors.empty()) {
        std::cout << '\n';
    }

    std::cout << "]";
}

void print_profiles_json(const Snapshot& snapshot) {
    std::cout
        << "{\n  \"current\": ";
    print_json_optional_integer(snapshot.current_power);
    std::cout
        << ",\n  \"targetMode\": \""
        << target_mode_name(snapshot.target_state.mode)
        << "\",\n  \"profiles\": [";

    for (std::size_t index = 0; index < snapshot.power_profiles.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << static_cast<unsigned int>(snapshot.power_profiles[index]);
    }

    std::cout << "]\n}";
}

void print_status_human(const Snapshot& snapshot) {
    std::cout
        << "awfan native status\n"
        << "Instance: " << snapshot.instance_path << '\n'
        << "System signature: ";

    if (snapshot.system_signature.has_value()) {
        std::cout << hex32(*snapshot.system_signature);
    } else {
        std::cout << "unavailable";
    }

    std::cout << "\nPower profile: ";
    if (snapshot.current_power.has_value()) {
        std::cout << profile_label(snapshot, *snapshot.current_power);
    } else {
        std::cout << "unavailable";
    }

    std::cout
        << "\nTarget source: "
        << target_mode_name(snapshot.target_state.mode)
        << "\n\nFans:\n";
    print_fans_human(snapshot);
    std::cout << "\n\nTemperatures:\n";
    print_temps_human(snapshot);
}

void print_status_json(const Snapshot& snapshot) {
    std::cout
        << "{\n"
        << "  \"instancePath\": \""
        << json_escape(snapshot.instance_path) << "\",\n"
        << "  \"systemSignature\": ";

    if (snapshot.system_signature.has_value()) {
        std::cout
            << "{\"raw\": " << *snapshot.system_signature
            << ", \"hex\": \"" << hex32(*snapshot.system_signature)
            << "\"}";
    } else {
        std::cout << "null";
    }

    std::cout << ",\n  \"currentPowerProfile\": ";
    print_json_optional_integer(snapshot.current_power);
    std::cout
        << ",\n  \"targetMode\": \""
        << target_mode_name(snapshot.target_state.mode)
        << "\",\n  \"targetStateUpdatedEpoch\": ";

    if (snapshot.target_state.updated_epoch.has_value()) {
        std::cout << *snapshot.target_state.updated_epoch;
    } else {
        std::cout << "null";
    }

    std::cout << ",\n  \"powerProfiles\": [";
    for (std::size_t index = 0; index < snapshot.power_profiles.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << static_cast<unsigned int>(snapshot.power_profiles[index]);
    }

    std::cout << "],\n  \"fans\": ";
    print_fans_json(snapshot);
    std::cout << ",\n  \"sensors\": ";
    print_temps_json(snapshot);
    std::cout << "\n}\n";
}

void require_write_confirmation(const bool confirmed) {
    if (!confirmed) {
        throw std::runtime_error(
            "Hardware writes are blocked without --yes. Review the command, "
            "then rerun it with --yes."
        );
    }
}

void require_successful_write(
    const CallResult& result,
    const std::string& operation
) {
    if (!result.succeeded()) {
        throw std::runtime_error(
            operation + " failed at the WMI provider (" +
            hresult_text(result.hresult) + ")"
        );
    }

    if (result.signed_value() < 0) {
        throw std::runtime_error(
            operation + " returned firmware result " +
            std::to_string(result.signed_value()) + "."
        );
    }
}

int run_read_operation(
    const bool json_output,
    const int mode
) {
    ComRuntime runtime;
    const AwccClient client;
    const Snapshot snapshot = collect_snapshot(client);

    switch (mode) {
        case 0:
            if (json_output) {
                print_status_json(snapshot);
            } else {
                print_status_human(snapshot);
            }
            break;
        case 1:
            if (json_output) {
                print_temps_json(snapshot);
                std::cout << '\n';
            } else {
                print_temps_human(snapshot);
            }
            break;
        case 2:
            if (json_output) {
                print_fans_json(snapshot);
                std::cout << '\n';
            } else {
                print_fans_human(snapshot);
            }
            break;
        case 3:
            if (json_output) {
                print_profiles_json(snapshot);
                std::cout << '\n';
            } else {
                print_profiles_human(snapshot);
            }
            break;
        default:
            throw std::runtime_error("Internal read mode was invalid.");
    }

    return 0;
}

}  // namespace

int run_native_status(const bool json_output) {
    return run_read_operation(json_output, 0);
}

int run_native_temps(const bool json_output) {
    return run_read_operation(json_output, 1);
}

int run_native_fans(const bool json_output) {
    return run_read_operation(json_output, 2);
}

int run_native_profiles(const bool json_output) {
    return run_read_operation(json_output, 3);
}

int run_native_watch(
    const int seconds,
    const bool temperatures_only
) {
    if (seconds < 1 || seconds > 60) {
        throw std::runtime_error("Watch interval must be from 1 to 60 seconds.");
    }

    ComRuntime runtime;
    const AwccClient client;

    while (true) {
        const Snapshot snapshot = collect_snapshot(client);
        std::cout << "\x1b[2J\x1b[H";
        std::cout
            << "awfan " << (temperatures_only ? "temps" : "watch")
            << " - refresh " << seconds << "s\n\n";

        if (temperatures_only) {
            print_temps_human(snapshot);
        } else {
            print_status_human(snapshot);
        }

        std::cout << "\nPress Ctrl+C to stop.\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }
}

int run_native_set_boost(
    const int cpu_percent,
    const int gpu_percent,
    const bool confirmed,
    const bool json_output
) {
    require_write_confirmation(confirmed);

    if (cpu_percent < 0 || cpu_percent > 100
        || gpu_percent < 0 || gpu_percent > 100) {
        throw std::runtime_error("Fan boost values must be from 0 to 100.");
    }

    ComRuntime runtime;
    const AwccClient client;

    if (client.fan_ids().size() < 2) {
        throw std::runtime_error(
            "Fewer than two firmware fans were discovered; refusing to write."
        );
    }

    require_successful_write(
        client.set_power_raw(0x00),
        "Switching to manual fan control"
    );
    require_successful_write(
        client.set_fan_boost(
            client.fan_ids()[0],
            static_cast<std::uint8_t>(cpu_percent)
        ),
        "Setting CPU fan boost"
    );
    require_successful_write(
        client.set_fan_boost(
            client.fan_ids()[1],
            static_cast<std::uint8_t>(gpu_percent)
        ),
        "Setting GPU fan boost"
    );

    TargetState state;
    state.mode = TargetMode::manual;
    state.cpu_percent = cpu_percent;
    state.gpu_percent = gpu_percent;
    state.updated_epoch = current_epoch_seconds();
    save_target_state(state);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const Snapshot snapshot = collect_snapshot(client);

    if (json_output) {
        std::cout
            << "{\"success\": true, \"mode\": \"manual\", "
            << "\"requested\": {\"cpu\": " << cpu_percent
            << ", \"gpu\": " << gpu_percent << "}, "
            << "\"status\": ";
        print_status_json(snapshot);
        std::cout << "}\n";
    } else {
        std::cout
            << "Manual fan control enabled.\n"
            << "Requested target: CPU " << cpu_percent
            << "%, GPU " << gpu_percent << "%\n"
            << "Estimated target RPM is derived from each fan's maximum RPM.\n\n";
        print_fans_human(snapshot);
    }

    return 0;
}

int run_native_set_profile(
    const int profile_index,
    const bool confirmed,
    const bool json_output
) {
    require_write_confirmation(confirmed);

    ComRuntime runtime;
    const AwccClient client;
    const auto& profiles = client.power_profiles();

    if (profile_index < 0
        || static_cast<std::size_t>(profile_index) >= profiles.size()) {
        throw std::runtime_error(
            "Profile index is unavailable. Run 'awfan-native profiles' first."
        );
    }

    const std::uint8_t raw_profile = profiles[
        static_cast<std::size_t>(profile_index)
    ];

    if (profile_index != 0) {
        for (const std::uint8_t fan_id : client.fan_ids()) {
            require_successful_write(
                client.set_fan_boost(fan_id, 0),
                "Returning a fan to firmware control"
            );
        }
    }

    require_successful_write(
        client.set_power_raw(raw_profile),
        "Setting the power profile"
    );

    TargetState state;
    state.mode = profile_index == 0
        ? TargetMode::manual
        : TargetMode::firmware;
    state.profile_index = profile_index;
    state.raw_profile = raw_profile;
    state.updated_epoch = current_epoch_seconds();
    save_target_state(state);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const Snapshot snapshot = collect_snapshot(client);

    if (json_output) {
        std::cout
            << "{\"success\": true, \"requestedIndex\": "
            << profile_index << ", \"requestedRaw\": "
            << static_cast<unsigned int>(raw_profile)
            << ", \"currentRaw\": ";
        print_json_optional_integer(snapshot.current_power);
        std::cout << "}\n";
    } else {
        std::cout
            << "Requested profile " << profile_index
            << " (" << hex8(raw_profile) << ").\n"
            << "Current profile: ";

        if (snapshot.current_power.has_value()) {
            std::cout << profile_label(snapshot, *snapshot.current_power);
        } else {
            std::cout << "unavailable";
        }

        if (profile_index == 0) {
            std::cout
                << "\nManual mode selected. The exact fixed fan target remains "
                << "unknown until a native boost command is issued.";
        } else {
            std::cout
                << "\nFan targets are now firmware-controlled and change with "
                << "temperature.";
        }
        std::cout << '\n';
    }

    return 0;
}

}  // namespace awfan
