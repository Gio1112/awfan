#pragma once

namespace awfan {

int run_native_status(bool json_output);
int run_native_temps(bool json_output);
int run_native_fans(bool json_output);
int run_native_profiles(bool json_output);
int run_native_watch(int seconds, bool temperatures_only);
int run_native_set_boost(int cpu_percent, int gpu_percent, bool confirmed, bool json_output);
int run_native_set_profile(int profile_index, bool confirmed, bool json_output);

}  // namespace awfan
