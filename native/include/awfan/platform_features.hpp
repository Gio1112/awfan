#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace awfan {

struct RestoreTarget {
    int profile_index{0};
    int raw_profile{0};
};

struct TimerState {
    int restore_profile_index{0};
    std::optional<std::int64_t> expires_epoch_ms;
    bool restore_on_start{false};
};

struct Preset {
    std::string name;
    int cpu_value{0};
    int gpu_value{0};
};

std::string profile_name_from_index(int profile_index);
std::string profile_name_from_raw(int raw_profile);
int profile_index_from_name(const std::wstring& name);
std::string decorate_profile_output(
    const std::wstring& command,
    const std::string& output
);

std::int64_t current_epoch_milliseconds();
std::int64_t parse_duration_milliseconds(const std::wstring& text);
std::string format_duration(std::int64_t milliseconds);

std::optional<RestoreTarget> load_restore_target();
void save_restore_target(const RestoreTarget& target);

std::optional<TimerState> load_timer_state();
void save_timer_state(const TimerState& state);
void clear_timer_state();

std::vector<Preset> load_presets();
void save_presets(const std::vector<Preset>& presets);
std::optional<Preset> find_preset(
    const std::vector<Preset>& presets,
    const std::string& name
);
bool valid_preset_name(const std::string& name);

std::wstring local_awfan_directory();
std::wstring broker_log_path();
void append_broker_log(std::string_view message);
std::vector<std::string> tail_broker_log(std::size_t line_count);

std::string json_escape_text(std::string_view value);
std::string redact_diagnostic_text(std::string value);
std::string utf8_from_wide(const std::wstring& value);
std::wstring wide_from_utf8(const std::string& value);

}  // namespace awfan
