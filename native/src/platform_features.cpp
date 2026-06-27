#include "awfan/platform_features.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace awfan {
namespace {

namespace fs = std::filesystem;

std::mutex& file_mutex() {
    static std::mutex mutex;
    return mutex;
}

fs::path restore_path() {
    return fs::path(local_awfan_directory()) / L"restore-v1.txt";
}

fs::path timer_path() {
    return fs::path(local_awfan_directory()) / L"timer-v1.txt";
}

fs::path presets_path() {
    return fs::path(local_awfan_directory()) / L"presets.json";
}

void atomic_write(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    const fs::path temporary = path.wstring() + L".tmp";

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Could not write awfan feature state.");
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(path, error);
        error.clear();
        fs::rename(temporary, path, error);
    }

    if (error) {
        fs::remove(temporary, error);
        throw std::runtime_error("Could not replace awfan feature state.");
    }
}

std::string read_all_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

std::optional<std::string> key_value(
    const std::string& text,
    const std::string& key
) {
    std::istringstream input(text);
    std::string line;
    const std::string prefix = key + "=";

    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }

    return std::nullopt;
}

std::optional<int> parse_int(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(*value, &consumed, 10);
        if (consumed == value->size()) {
            return parsed;
        }
    } catch (...) {
    }

    return std::nullopt;
}

std::optional<std::int64_t> parse_i64(
    const std::optional<std::string>& value
) {
    if (!value.has_value() || value->empty()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const std::int64_t parsed = std::stoll(*value, &consumed, 10);
        if (consumed == value->size()) {
            return parsed;
        }
    } catch (...) {
    }

    return std::nullopt;
}

void replace_all(
    std::string& value,
    const std::string& before,
    const std::string& after
) {
    std::size_t position = 0;
    while ((position = value.find(before, position)) != std::string::npos) {
        value.replace(position, before.size(), after);
        position += after.size();
    }
}

std::optional<int> parse_json_integer_after(
    const std::string& text,
    const std::string& marker
) {
    const std::size_t marker_position = text.find(marker);
    if (marker_position == std::string::npos) {
        return std::nullopt;
    }

    std::size_t position = marker_position + marker.size();
    while (position < text.size()
        && std::isspace(static_cast<unsigned char>(text[position]))) {
        ++position;
    }

    if (text.compare(position, 4, "null") == 0) {
        return std::nullopt;
    }

    std::size_t end = position;
    if (end < text.size() && text[end] == '-') {
        ++end;
    }
    while (end < text.size()
        && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }

    if (end == position) {
        return std::nullopt;
    }

    try {
        return std::stoi(text.substr(position, end - position));
    } catch (...) {
        return std::nullopt;
    }
}

void insert_json_name_after_integer(
    std::string& text,
    const std::string& marker,
    const std::string& name_field
) {
    const std::size_t marker_position = text.find(marker);
    if (marker_position == std::string::npos) {
        return;
    }

    std::size_t position = marker_position + marker.size();
    while (position < text.size()
        && std::isspace(static_cast<unsigned char>(text[position]))) {
        ++position;
    }

    if (text.compare(position, 4, "null") == 0) {
        return;
    }

    std::size_t end = position;
    if (end < text.size() && text[end] == '-') {
        ++end;
    }
    while (end < text.size()
        && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }

    if (end == position) {
        return;
    }

    int raw = 0;
    try {
        raw = std::stoi(text.substr(position, end - position));
    } catch (...) {
        return;
    }

    const std::string name = profile_name_from_raw(raw);
    text.insert(
        end,
        ", \"" + name_field + "\": \"" + json_escape_text(name) + "\""
    );
}

std::string lowercase_ascii(std::wstring value) {
    std::string output;
    output.reserve(value.size());

    for (wchar_t character : value) {
        if (character == L'_' || character == L' ') {
            character = L'-';
        }
        if (character >= L'A' && character <= L'Z') {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
        if (character >= 0 && character <= 0x7f) {
            output.push_back(static_cast<char>(character));
        }
    }

    return output;
}

std::string environment_utf8(const wchar_t* name) {
    const wchar_t* value = _wgetenv(name);
    return value == nullptr ? std::string{} : utf8_from_wide(value);
}

}  // namespace

std::string profile_name_from_index(const int profile_index) {
    switch (profile_index) {
        case 0: return "Manual";
        case 1: return "Balanced";
        case 2: return "Balanced Performance";
        case 3: return "Cool";
        case 4: return "Quiet";
        case 5: return "Performance";
        default: return "Firmware profile " + std::to_string(profile_index);
    }
}

std::string profile_name_from_raw(const int raw_profile) {
    switch (raw_profile) {
        case 0x00: return "Manual";
        case 0xA0: return "Balanced";
        case 0xA1: return "Balanced Performance";
        case 0xA2: return "Cool";
        case 0xA3: return "Quiet";
        case 0xA4: return "Performance";
        default: {
            std::ostringstream output;
            output << "Unknown 0x" << std::hex << std::uppercase
                   << std::setw(2) << std::setfill('0')
                   << (raw_profile & 0xff);
            return output.str();
        }
    }
}

int profile_index_from_name(const std::wstring& name) {
    const std::string normalized = lowercase_ascii(name);

    if (normalized == "balanced") {
        return 1;
    }
    if (normalized == "balanced-performance"
        || normalized == "balancedperformance") {
        return 2;
    }
    if (normalized == "cool") {
        return 3;
    }
    if (normalized == "quiet") {
        return 4;
    }
    if (normalized == "performance") {
        return 5;
    }

    try {
        std::size_t consumed = 0;
        const int index = std::stoi(normalized, &consumed, 10);
        if (consumed == normalized.size() && index >= 1 && index <= 5) {
            return index;
        }
    } catch (...) {
    }

    return 0;
}

std::string decorate_profile_output(
    const std::wstring& command,
    const std::string& output
) {
    std::string decorated = output;

    for (int index = 1; index <= 5; ++index) {
        const int raw = 0x9f + index;
        std::ostringstream raw_hex;
        raw_hex << "0x" << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0') << raw;

        replace_all(
            decorated,
            "Firmware profile " + std::to_string(index) + " (" + raw_hex.str() + ")",
            profile_name_from_index(index) + " (" + raw_hex.str() + ")"
        );
        replace_all(
            decorated,
            "Requested profile " + std::to_string(index) + " (" + raw_hex.str() + ").",
            "Requested " + profile_name_from_index(index) + " (profile "
                + std::to_string(index) + ", " + raw_hex.str() + ")."
        );
    }

    if (!decorated.empty() && decorated.front() == '{') {
        if (command == L"status") {
            insert_json_name_after_integer(
                decorated,
                "\"currentPowerProfile\":",
                "currentPowerProfileName"
            );
        } else if (command == L"profiles" || command == L"profile-list") {
            insert_json_name_after_integer(
                decorated,
                "\"current\":",
                "currentName"
            );

            const std::size_t closing = decorated.rfind('}');
            if (closing != std::string::npos) {
                const std::string names =
                    ",\n  \"profileNames\": [\"Manual\", \"Balanced\", "
                    "\"Balanced Performance\", \"Cool\", \"Quiet\", "
                    "\"Performance\"]\n";
                decorated.insert(closing, names);
            }
        } else if (command == L"profile" || command == L"auto") {
            insert_json_name_after_integer(
                decorated,
                "\"requestedRaw\":",
                "requestedName"
            );
            insert_json_name_after_integer(
                decorated,
                "\"currentRaw\":",
                "currentName"
            );
        }
    }

    return decorated;
}

std::int64_t current_epoch_milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::int64_t parse_duration_milliseconds(const std::wstring& text) {
    if (text.size() < 2) {
        throw std::runtime_error(
            "Duration must use a suffix such as 30s, 20m, 2h, or 1d."
        );
    }

    const wchar_t suffix = text.back();
    const std::wstring number = text.substr(0, text.size() - 1);
    std::size_t consumed = 0;
    long long amount = 0;

    try {
        amount = std::stoll(number, &consumed, 10);
    } catch (...) {
        throw std::runtime_error("Duration amount must be an integer.");
    }

    if (consumed != number.size() || amount <= 0) {
        throw std::runtime_error("Duration amount must be greater than zero.");
    }

    std::int64_t multiplier = 0;
    switch (suffix) {
        case L's': case L'S': multiplier = 1000LL; break;
        case L'm': case L'M': multiplier = 60LL * 1000LL; break;
        case L'h': case L'H': multiplier = 60LL * 60LL * 1000LL; break;
        case L'd': case L'D': multiplier = 24LL * 60LL * 60LL * 1000LL; break;
        default:
            throw std::runtime_error(
                "Duration suffix must be s, m, h, or d."
            );
    }

    const std::int64_t milliseconds = amount * multiplier;
    const std::int64_t maximum = 7LL * 24LL * 60LL * 60LL * 1000LL;
    if (milliseconds < 1000LL || milliseconds > maximum) {
        throw std::runtime_error("Duration must be from 1 second to 7 days.");
    }

    return milliseconds;
}

std::string format_duration(const std::int64_t milliseconds) {
    const std::int64_t seconds = milliseconds / 1000;
    if (seconds % 86400 == 0) {
        return std::to_string(seconds / 86400) + "d";
    }
    if (seconds % 3600 == 0) {
        return std::to_string(seconds / 3600) + "h";
    }
    if (seconds % 60 == 0) {
        return std::to_string(seconds / 60) + "m";
    }
    return std::to_string(seconds) + "s";
}

std::optional<RestoreTarget> load_restore_target() {
    std::lock_guard lock(file_mutex());
    const std::string text = read_all_text(restore_path());
    const auto index = parse_int(key_value(text, "profile_index"));
    const auto raw = parse_int(key_value(text, "raw_profile"));

    if (!index.has_value() || !raw.has_value()
        || *index < 1 || *index > 63 || *raw <= 0 || *raw > 255) {
        return std::nullopt;
    }

    return RestoreTarget{*index, *raw};
}

void save_restore_target(const RestoreTarget& target) {
    if (target.profile_index < 1 || target.raw_profile <= 0) {
        throw std::runtime_error("Invalid restore target.");
    }

    std::lock_guard lock(file_mutex());
    std::ostringstream output;
    output << "profile_index=" << target.profile_index << '\n'
           << "raw_profile=" << target.raw_profile << '\n';
    atomic_write(restore_path(), output.str());
}

std::optional<TimerState> load_timer_state() {
    std::lock_guard lock(file_mutex());
    const std::string text = read_all_text(timer_path());
    const auto index = parse_int(key_value(text, "restore_profile_index"));
    if (!index.has_value() || *index < 1 || *index > 63) {
        return std::nullopt;
    }

    TimerState state;
    state.restore_profile_index = *index;
    state.expires_epoch_ms = parse_i64(key_value(text, "expires_epoch_ms"));
    state.restore_on_start = key_value(text, "restore_on_start").value_or("0") == "1";

    if (!state.expires_epoch_ms.has_value() && !state.restore_on_start) {
        return std::nullopt;
    }

    return state;
}

void save_timer_state(const TimerState& state) {
    if (state.restore_profile_index < 1 || state.restore_profile_index > 63) {
        throw std::runtime_error("Invalid timer restore profile.");
    }

    std::lock_guard lock(file_mutex());
    std::ostringstream output;
    output << "restore_profile_index=" << state.restore_profile_index << '\n'
           << "expires_epoch_ms=";
    if (state.expires_epoch_ms.has_value()) {
        output << *state.expires_epoch_ms;
    }
    output << '\n'
           << "restore_on_start=" << (state.restore_on_start ? 1 : 0) << '\n';
    atomic_write(timer_path(), output.str());
}

void clear_timer_state() {
    std::lock_guard lock(file_mutex());
    std::error_code error;
    fs::remove(timer_path(), error);
}

std::vector<Preset> load_presets() {
    std::lock_guard lock(file_mutex());
    const std::string text = read_all_text(presets_path());
    std::vector<Preset> presets;

    const std::regex item(
        R"(\{\s*"name"\s*:\s*"([A-Za-z0-9_-]+)"\s*,\s*"cpu"\s*:\s*([0-9]{1,3})\s*,\s*"gpu"\s*:\s*([0-9]{1,3})\s*\})"
    );

    for (std::sregex_iterator iterator(text.begin(), text.end(), item), end;
         iterator != end;
         ++iterator) {
        Preset preset;
        preset.name = (*iterator)[1].str();
        preset.cpu_value = std::stoi((*iterator)[2].str());
        preset.gpu_value = std::stoi((*iterator)[3].str());

        if (preset.cpu_value >= 0 && preset.cpu_value <= 100
            && preset.gpu_value >= 0 && preset.gpu_value <= 100) {
            presets.push_back(std::move(preset));
        }
    }

    std::sort(
        presets.begin(),
        presets.end(),
        [](const Preset& left, const Preset& right) {
            return left.name < right.name;
        }
    );
    return presets;
}

void save_presets(const std::vector<Preset>& presets) {
    std::lock_guard lock(file_mutex());
    std::vector<Preset> ordered = presets;
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const Preset& left, const Preset& right) {
            return left.name < right.name;
        }
    );

    std::ostringstream output;
    output << "{\n  \"version\": 1,\n  \"presets\": [";
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const Preset& preset = ordered[index];
        output << (index == 0 ? "\n" : ",\n")
               << "    {\"name\": \"" << json_escape_text(preset.name)
               << "\", \"cpu\": " << preset.cpu_value
               << ", \"gpu\": " << preset.gpu_value << "}";
    }
    if (!ordered.empty()) {
        output << '\n';
    }
    output << "  ]\n}\n";
    atomic_write(presets_path(), output.str());
}

std::optional<Preset> find_preset(
    const std::vector<Preset>& presets,
    const std::string& name
) {
    const auto found = std::find_if(
        presets.begin(),
        presets.end(),
        [&](const Preset& preset) {
            return preset.name == name;
        }
    );

    if (found == presets.end()) {
        return std::nullopt;
    }
    return *found;
}

bool valid_preset_name(const std::string& name) {
    if (name.empty() || name.size() > 32) {
        return false;
    }

    return std::all_of(
        name.begin(),
        name.end(),
        [](const unsigned char character) {
            return std::isalnum(character) || character == '-' || character == '_';
        }
    );
}

std::wstring local_awfan_directory() {
    const wchar_t* local_app_data = _wgetenv(L"LOCALAPPDATA");
    if (local_app_data == nullptr || *local_app_data == L'\0') {
        throw std::runtime_error("LOCALAPPDATA is unavailable.");
    }
    return (fs::path(local_app_data) / L"awfan").wstring();
}

std::wstring broker_log_path() {
    return (fs::path(local_awfan_directory()) / L"broker.log").wstring();
}

void append_broker_log(const std::string_view message) {
    std::lock_guard lock(file_mutex());
    fs::create_directories(fs::path(broker_log_path()).parent_path());
    std::ofstream output(fs::path(broker_log_path()), std::ios::app);
    if (!output) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t current = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &current);

    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
           << "  " << message << '\n';
}

std::vector<std::string> tail_broker_log(const std::size_t line_count) {
    std::lock_guard lock(file_mutex());
    std::ifstream input(fs::path(broker_log_path()));
    std::vector<std::string> lines;
    std::string line;

    while (std::getline(input, line)) {
        lines.push_back(line);
        if (lines.size() > line_count) {
            lines.erase(lines.begin());
        }
    }

    return lines;
}

std::string json_escape_text(const std::string_view value) {
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
                    output << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<int>(character)
                           << std::dec;
                } else {
                    output << static_cast<char>(character);
                }
                break;
        }
    }
    return output.str();
}

std::string redact_diagnostic_text(std::string value) {
    std::array<std::pair<std::string, std::string>, 5> replacements{
        std::pair{environment_utf8(L"COMPUTERNAME"), std::string("<computer>")},
        std::pair{environment_utf8(L"USERNAME"), std::string("<user>")},
        std::pair{environment_utf8(L"USERPROFILE"), std::string("<user-profile>")},
        std::pair{environment_utf8(L"LOCALAPPDATA"), std::string("<local-app-data>")},
        std::pair{environment_utf8(L"APPDATA"), std::string("<app-data>")}
    };

    std::sort(
        replacements.begin(),
        replacements.end(),
        [](const auto& left, const auto& right) {
            return left.first.size() > right.first.size();
        }
    );

    for (const auto& [before, after] : replacements) {
        if (!before.empty()) {
            replace_all(value, before, after);
        }
    }

    return value;
}

std::string utf8_from_wide(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (required <= 0) {
        return {};
    }

    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        required,
        nullptr,
        nullptr
    );
    return output;
}

std::wstring wide_from_utf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (required <= 0) {
        return {};
    }

    std::wstring output(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        required
    );
    return output;
}

}  // namespace awfan
