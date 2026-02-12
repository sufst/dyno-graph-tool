#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

struct channel_def {
    const char* name;
    const char* unit;
};

inline constexpr int num_channels = 41;

inline constexpr channel_def channel_defs[num_channels] = {
    {"raw_enc_counter",  "counts"},
    {"raw_enc2_pos",     "counts"},
    {"elapsed_time",     "s"},
    {"raw_hw_counter3",  "counts"},
    {"raw_hw_counter4",  "counts"},
    {"engine_rpm",       "RPM"},
    {"raw_hw_counter6",  "counts"},
    {"roller_distance",  "m"},
    {"roller_omega",     "rad/s"},
    {"wheel_speed_mph",  "mph"},
    {"expansion_1",  ""}, {"expansion_2",  ""}, {"expansion_3",  ""},
    {"expansion_4",  ""}, {"expansion_5",  ""}, {"expansion_6",  ""},
    {"expansion_7",  ""}, {"expansion_8",  ""}, {"expansion_9",  ""},
    {"expansion_10", ""}, {"expansion_11", ""}, {"expansion_12", ""},
    {"air_temp",         "C"},
    {"baro_pressure",    "mbar"},
    {"humidity",         "%"},
    {"aux_channel",      ""},
    {"cooler_temp",      "C"},
    {"load_cell_temp",   "C"},
    {"load_cell_torque", "ft-lb"},
    {"tacho_rpm",        "RPM"},
    {"brake_load_cmd",   "%"},
    {"raw_enc_delta",    "counts"},
    {"load_cell_state",  ""},
    {"brake_active",     ""},
    {"reserved_34", ""}, {"reserved_35", ""}, {"reserved_36", ""},
    {"reserved_37", ""}, {"reserved_38", ""}, {"reserved_39", ""},
    {"reserved_40", ""},
};

enum ch : int {
    ch_elapsed_time   = 2,
    ch_engine_rpm     = 5,
    ch_roller_omega   = 8,
    ch_wheel_speed    = 9,
};

struct dpr_header {
    std::string date, time, filename, run_name;
    int run_number = 0;

    double ambient_temp_c    = 0;
    double ambient_press_mb  = 0;
    double ambient_humid_pct = 0;
    double correction_factor = 0;

    double roller_circ_ft = 0;
    double roller_diam_in = 0;
    double wheel_circ_m   = 0;
    double gear_ratio     = 0;
    std::string machine_sub;
    std::array<double, 4> friction_poly = {0, 0, 0, 0};

    std::string manufacturer, model, machine_type, software_version;
    int opto_slots = 0;

    double peak_power_hp    = 0, peak_power_rpm  = 0;
    double peak_torque_ftlb = 0, peak_torque_rpm = 0;

    double roller_inertia = 0;
};

struct dpr_run {
    dpr_header header;
    int num_rows    = 0;
    int num_columns = 0;
    std::vector<std::vector<double>> data;

    auto& channel(int c) const { return data[c]; }
    bool has_channel(int c) const { return c >= 0 && c < num_columns && !data[c].empty(); }
};

std::optional<dpr_run> parse_dpr_file(const std::string& path, std::string& err);
